#!/usr/bin/env python3
"""Fit the tiny runtime policy/value nets to Monte-Carlo RL-vs-greedy returns.

The C++ generator owns rules, legal actions, topology, and rollouts. This script
never invents teacher labels: it consumes the returns produced by those games,
keeps a group-disjoint validation split, and reports a separate structural
holdout whose map/unit ranges are outside the training distribution.
"""

from __future__ import annotations

import argparse
import copy
import csv
import hashlib
import json
import math
from dataclasses import dataclass
from pathlib import Path

import torch
from torch import nn


FEATURES = 24
HIDDEN = 32
DEFAULT_SEED = 0x5EED1234


class TinyNet(nn.Module):
    def __init__(self, bounded: bool) -> None:
        super().__init__()
        self.hidden = nn.Linear(FEATURES, HIDDEN)
        self.output = nn.Linear(HIDDEN, 1)
        self.bounded = bounded

    def forward(self, values: torch.Tensor) -> torch.Tensor:
        result = self.output(torch.tanh(self.hidden(values))).squeeze(-1)
        return torch.tanh(result) if self.bounded else result


@dataclass
class Rows:
    features: torch.Tensor
    targets: torch.Tensor
    groups: torch.Tensor

    def subset(self, mask: torch.Tensor) -> "Rows":
        return Rows(self.features[mask], self.targets[mask], self.groups[mask])


def load_dataset(path: Path) -> dict[tuple[str, str], Rows]:
    buckets: dict[tuple[str, str], tuple[list[list[float]], list[float], list[int]]] = {}
    with path.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        expected = {"split", "head", "group", "target", *(f"f{i}" for i in range(FEATURES))}
        if set(reader.fieldnames or []) != expected:
            raise ValueError(f"unexpected dataset columns in {path}")
        for row in reader:
            key = (row["split"], row["head"])
            features, targets, groups = buckets.setdefault(key, ([], [], []))
            features.append([float(row[f"f{i}"]) for i in range(FEATURES)])
            targets.append(float(row["target"]))
            groups.append(int(row["group"]))

    result: dict[tuple[str, str], Rows] = {}
    for key, (features, targets, groups) in buckets.items():
        result[key] = Rows(
            torch.tensor(features, dtype=torch.float32),
            torch.tensor(targets, dtype=torch.float32),
            torch.tensor(groups, dtype=torch.int64),
        )
    for required in (("train", "P"), ("train", "V"), ("holdout", "P"), ("holdout", "V")):
        if required not in result or result[required].targets.numel() == 0:
            raise ValueError(f"dataset has no rows for {required}")
    return result


def split_train_validation(rows: Rows) -> tuple[Rows, Rows]:
    # Position/group-disjoint: actions from one board state can never leak across splits.
    validation_mask = torch.remainder(rows.groups, 10) == 0
    return rows.subset(~validation_mask), rows.subset(validation_mask)


def make_preference_pairs(rows: Rows, margin: float = 0.05) -> torch.Tensor:
    by_group: dict[int, list[int]] = {}
    for index, group in enumerate(rows.groups.tolist()):
        by_group.setdefault(group, []).append(index)
    pairs: list[tuple[int, int]] = []
    for indices in by_group.values():
        ordered = sorted(indices, key=lambda index: float(rows.targets[index]), reverse=True)
        for better_offset, better in enumerate(ordered):
            for worse in ordered[better_offset + 1 :]:
                if float(rows.targets[better] - rows.targets[worse]) >= margin:
                    pairs.append((better, worse))
    if not pairs:
        return torch.empty((0, 2), dtype=torch.int64)
    return torch.tensor(pairs, dtype=torch.int64)


def train_model(
    model: TinyNet,
    training: Rows,
    validation: Rows,
    epochs: int,
    batch_size: int,
    seed: int,
    preference_pairs: torch.Tensor | None = None,
) -> dict[str, float | int]:
    generator = torch.Generator().manual_seed(seed)
    optimizer = torch.optim.AdamW(model.parameters(), lr=2.0e-3, weight_decay=2.0e-5)
    best_state = copy.deepcopy(model.state_dict())
    best_validation = math.inf
    best_epoch = 0
    patience = max(6, epochs // 5)
    stale = 0
    validation_pairs = make_preference_pairs(validation) if preference_pairs is not None else None

    for epoch in range(epochs):
        permutation = torch.randperm(training.targets.shape[0], generator=generator)
        for start in range(0, training.targets.shape[0], batch_size):
            indexes = permutation[start : start + batch_size]
            prediction = model(training.features[indexes])
            target = training.targets[indexes]
            weights = 0.75 + 0.5 * target.abs()
            regression = (
                nn.functional.smooth_l1_loss(prediction, target, beta=0.20, reduction="none") * weights
            ).mean()
            loss = regression

            if preference_pairs is not None and preference_pairs.shape[0] > 0:
                pair_count = min(indexes.shape[0], preference_pairs.shape[0])
                pair_indexes = torch.randint(
                    0, preference_pairs.shape[0], (pair_count,), generator=generator
                )
                pairs = preference_pairs[pair_indexes]
                better = model(training.features[pairs[:, 0]])
                worse = model(training.features[pairs[:, 1]])
                preference = nn.functional.softplus(-(better - worse) * 3.0).mean()
                loss = loss + 0.40 * preference

            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            optimizer.step()

        with torch.no_grad():
            validation_mae = float(
                (model(validation.features) - validation.targets).abs().mean()
            )
            validation_objective = validation_mae
            if validation_pairs is not None and validation_pairs.shape[0] > 0:
                better = model(validation.features[validation_pairs[:, 0]])
                worse = model(validation.features[validation_pairs[:, 1]])
                validation_objective += 0.40 * float(
                    nn.functional.softplus(-(better - worse) * 3.0).mean()
                )
        if validation_objective < best_validation - 1.0e-5:
            best_validation = validation_objective
            best_epoch = epoch + 1
            best_state = copy.deepcopy(model.state_dict())
            stale = 0
        else:
            stale += 1
            if stale >= patience:
                break

    model.load_state_dict(best_state)
    return {
        "best_epoch": best_epoch,
        "epochs_requested": epochs,
        "validation_objective_at_selection": best_validation,
    }


def regression_metrics(model: TinyNet, rows: Rows) -> dict[str, float | int]:
    with torch.no_grad():
        prediction = model(rows.features)
        error = prediction - rows.targets
        centered_prediction = prediction - prediction.mean()
        centered_target = rows.targets - rows.targets.mean()
        denominator = torch.sqrt(
            torch.sum(centered_prediction.square()) * torch.sum(centered_target.square())
        )
        correlation = float(torch.sum(centered_prediction * centered_target) / denominator) if denominator > 0 else 0.0
        decisive = rows.targets.abs() >= 0.20
        sign_accuracy = (
            float((torch.sign(prediction[decisive]) == torch.sign(rows.targets[decisive])).float().mean())
            if decisive.any()
            else 0.0
        )
        return {
            "rows": int(rows.targets.shape[0]),
            "groups": int(torch.unique(rows.groups).shape[0]),
            "mae": float(error.abs().mean()),
            "rmse": float(torch.sqrt(torch.mean(error.square()))),
            "pearson_correlation": correlation,
            "decisive_outcome_sign_accuracy": sign_accuracy,
            "decisive_rows": int(decisive.sum()),
        }


def policy_metrics(model: TinyNet, rows: Rows, margin: float = 0.05) -> dict[str, float | int]:
    with torch.no_grad():
        prediction = model(rows.features)
    by_group: dict[int, list[int]] = {}
    for index, group in enumerate(rows.groups.tolist()):
        by_group.setdefault(group, []).append(index)

    agreements = 0
    regrets: list[float] = []
    correct_pairs = 0
    pair_count = 0
    for indices in by_group.values():
        target_best = max(indices, key=lambda index: (float(rows.targets[index]), -index))
        predicted_best = max(indices, key=lambda index: (float(prediction[index]), -index))
        agreements += int(target_best == predicted_best)
        regrets.append(float(rows.targets[target_best] - rows.targets[predicted_best]))
        for left_offset, left in enumerate(indices):
            for right in indices[left_offset + 1 :]:
                target_delta = float(rows.targets[left] - rows.targets[right])
                if abs(target_delta) < margin:
                    continue
                predicted_delta = float(prediction[left] - prediction[right])
                correct_pairs += int((target_delta > 0.0) == (predicted_delta > 0.0))
                pair_count += 1

    return {
        "rows": int(rows.targets.shape[0]),
        "positions": len(by_group),
        "top1_action_agreement": agreements / max(1, len(by_group)),
        "mean_top1_return_regret": sum(regrets) / max(1, len(regrets)),
        "pairwise_ordering_accuracy_margin_0_05": correct_pairs / max(1, pair_count),
        "evaluated_action_pairs": pair_count,
    }


def center_policy_returns(rows: Rows) -> Rows:
    """Policy scores are compared only inside one position; remove irrelevant game offset."""
    centered = rows.targets.clone()
    for group in torch.unique(rows.groups):
        mask = rows.groups == group
        values = rows.targets[mask]
        shifted = values - values.mean()
        scale = torch.clamp(shifted.abs().max(), min=0.20)
        centered[mask] = shifted / scale
    return Rows(rows.features, centered, rows.groups)


def cpp_float(value: float) -> str:
    text = f"{value:.9g}"
    if "e" not in text and "." not in text:
        text += ".0"
    return text + "f"


def cpp_array(name: str, values: torch.Tensor) -> str:
    flat = values.detach().cpu().reshape(-1).tolist()
    lines = []
    for start in range(0, len(flat), 8):
        lines.append("\t\t" + ", ".join(cpp_float(item) for item in flat[start : start + 8]))
    return f"\tinline constexpr std::array<float, {len(flat)}> {name} =\n\t{{\n" + ",\n".join(lines) + "\n\t};"


def export_weights(path: Path, policy: TinyNet, value: TinyNet, report: dict[str, object]) -> str:
    policy_holdout = report["structural_holdout"]["policy"]
    value_holdout = report["structural_holdout"]["value"]
    sections = [
        "#pragma once",
        "",
        "// Generated by Tools/BotBenchmark/train_neural_planner.py from C++ RL-vs-greedy returns.",
        f"// seed={report['seed']} policy_top1_regret={policy_holdout['mean_top1_return_regret']:.6f} "
        f"value_mae={value_holdout['mae']:.6f}",
        "namespace OtherBios::BotNeural::Weights",
        "{",
        cpp_array("PolicyW1", policy.hidden.weight),
        cpp_array("PolicyB1", policy.hidden.bias),
        cpp_array("PolicyW2", policy.output.weight),
        f"\tinline constexpr float PolicyB2 = {cpp_float(float(policy.output.bias.detach()))};",
        cpp_array("ValueW1", value.hidden.weight),
        cpp_array("ValueB1", value.hidden.bias),
        cpp_array("ValueW2", value.output.weight),
        f"\tinline constexpr float ValueB2 = {cpp_float(float(value.output.bias.detach()))};",
        "}",
        "",
    ]
    content = "\n".join(sections)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")
    return hashlib.sha256(content.encode("utf-8")).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=60)
    parser.add_argument("--batch-size", type=int, default=1024)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--weights", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    weights_path = args.weights or root / "Source" / "OtherBios" / "HexBotNeuralWeights.inl"
    report_path = args.report or root / "Docs" / "metrics" / "neural_training.json"
    rows = load_dataset(args.data)
    policy_train_raw, policy_validation_raw = split_train_validation(rows[("train", "P")])
    policy_train = center_policy_returns(policy_train_raw)
    policy_validation = center_policy_returns(policy_validation_raw)
    value_train, value_validation = split_train_validation(rows[("train", "V")])

    torch.manual_seed(args.seed)
    torch.set_num_threads(max(1, min(8, torch.get_num_threads())))
    policy = TinyNet(bounded=False)
    value = TinyNet(bounded=True)
    policy_training = train_model(
        policy,
        policy_train,
        policy_validation,
        args.epochs,
        args.batch_size,
        args.seed,
        make_preference_pairs(policy_train),
    )
    value_training = train_model(
        value,
        value_train,
        value_validation,
        args.epochs,
        args.batch_size,
        args.seed ^ 0xA5A5A5A5,
    )

    metadata_path = Path(str(args.data) + ".meta.json")
    metadata = json.loads(metadata_path.read_text(encoding="utf-8")) if metadata_path.exists() else None
    report: dict[str, object] = {
        "seed": args.seed,
        "algorithm": "fitted_monte_carlo_policy_iteration",
        "target_source": "C++ rules-engine rollouts versus stronger deterministic greedy; no hand-written teacher labels",
        "dataset_sha256": hashlib.sha256(args.data.read_bytes()).hexdigest(),
        "dataset_metadata": metadata,
        "parameters": sum(parameter.numel() for parameter in policy.parameters())
        + sum(parameter.numel() for parameter in value.parameters()),
        "selection_validation": {
            "policy": {**policy_training, **policy_metrics(policy, policy_validation_raw)},
            "value": {**value_training, **regression_metrics(value, value_validation)},
        },
        "structural_holdout": {
            "note": "never used for gradients or early stopping; larger maps, armies, stat and rule ranges",
            "policy": policy_metrics(policy, rows[("holdout", "P")]),
            "value": regression_metrics(value, rows[("holdout", "V")]),
        },
    }
    report["weights_sha256"] = export_weights(weights_path, policy, value, report)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
