#include "HexUnitSpawnInfoCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HexUnitSpawnInfoCustomization"

TSharedRef<IPropertyTypeCustomization> FHexUnitSpawnInfoCustomization::MakeInstance()
{
	return MakeShared<FHexUnitSpawnInfoCustomization>();
}

void FHexUnitSpawnInfoCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& StructCustomizationUtils
)
{
	CachedStructPropertyHandle = StructPropertyHandle;

	HeaderRow
	.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(260.0f)
	.MaxDesiredWidth(520.0f)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			StructPropertyHandle->CreatePropertyValueWidget()
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(8.0f, 0.0f, 0.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("RemoveButtonText", "X"))
			.ToolTipText(LOCTEXT("RemoveButtonTooltip", "Delete this InitialUnits row. UnitClass, Coord and Enabled will be removed together."))
			.ContentPadding(FMargin(8.0f, 2.0f))
			.IsEnabled(this, &FHexUnitSpawnInfoCustomization::CanRemove)
			.OnClicked(this, &FHexUnitSpawnInfoCustomization::HandleRemoveClicked)
		]
	];
}

void FHexUnitSpawnInfoCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	IDetailChildrenBuilder& StructBuilder,
	IPropertyTypeCustomizationUtils& StructCustomizationUtils
)
{
	uint32 NumChildren = 0;
	StructPropertyHandle->GetNumChildren(NumChildren);

	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(ChildIndex);
		if (ChildHandle.IsValid() && ChildHandle->IsValidHandle())
		{
			StructBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}
}

bool FHexUnitSpawnInfoCustomization::CanRemove() const
{
	if (!CachedStructPropertyHandle.IsValid())
	{
		return false;
	}

	const int32 IndexInArray = CachedStructPropertyHandle->GetIndexInArray();
	if (IndexInArray == INDEX_NONE)
	{
		return false;
	}

	TSharedPtr<IPropertyHandle> ParentHandle = CachedStructPropertyHandle->GetParentHandle();
	if (!ParentHandle.IsValid())
	{
		return false;
	}

	TSharedPtr<IPropertyHandleArray> ArrayHandle = ParentHandle->AsArray();
	return ArrayHandle.IsValid();
}

FReply FHexUnitSpawnInfoCustomization::HandleRemoveClicked()
{
	if (!CanRemove())
	{
		return FReply::Handled();
	}

	const int32 IndexInArray = CachedStructPropertyHandle->GetIndexInArray();

	TSharedPtr<IPropertyHandle> ParentHandle = CachedStructPropertyHandle->GetParentHandle();
	if (!ParentHandle.IsValid())
	{
		return FReply::Handled();
	}

	TSharedPtr<IPropertyHandleArray> ArrayHandle = ParentHandle->AsArray();
	if (!ArrayHandle.IsValid())
	{
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("RemoveInitialUnitTransaction", "Remove Initial Unit Row"));

	ParentHandle->NotifyPreChange();
	ArrayHandle->DeleteItem(IndexInArray);
	ParentHandle->NotifyPostChange(EPropertyChangeType::ArrayRemove);

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
