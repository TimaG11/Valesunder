#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;

class FHexUnitSpawnInfoCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils
	) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		IDetailChildrenBuilder& StructBuilder,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils
	) override;

private:
	FReply HandleRemoveClicked();
	bool CanRemove() const;

private:
	TSharedPtr<IPropertyHandle> CachedStructPropertyHandle;
};
