// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuidFixer.h"
#include "GuidFixerStyle.h"
#include "GuidFixerCommands.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"

static const FName GuidFixerTabName("GuidFixer");

#define LOCTEXT_NAMESPACE "FGuidFixerModule"

void FGuidFixerModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	FGuidFixerStyle::Initialize();
	FGuidFixerStyle::ReloadTextures();

	FGuidFixerCommands::Register();

	FixMaterialGuidsCommands = MakeShareable(new FUICommandList);

	FixMaterialGuidsCommands->MapAction(
		FGuidFixerCommands::Get().FixMaterialGuids,
		FExecuteAction::CreateRaw(this, &FGuidFixerModule::FixMaterialGuids),
		FCanExecuteAction());

	FixTextureGuidsCommands = MakeShareable(new FUICommandList);

	FixTextureGuidsCommands->MapAction(
		FGuidFixerCommands::Get().FixTextureGuids,
		FExecuteAction::CreateRaw(this, &FGuidFixerModule::FixTextureGuids),
		FCanExecuteAction());

	FixEmptyTextureGuidsCommands = MakeShareable(new FUICommandList);

	FixEmptyTextureGuidsCommands->MapAction(
		FGuidFixerCommands::Get().FixEmptyTextureGuids,
		FExecuteAction::CreateRaw(this, &FGuidFixerModule::FixEmptyTextureGuids),
		FCanExecuteAction());


	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGuidFixerModule::RegisterMenus));
}

void FGuidFixerModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FGuidFixerStyle::Shutdown();

	FGuidFixerCommands::Unregister();
}

void FGuidFixerModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	{
		FToolMenuSection& Section = Menu->AddSection("GuidFixer", LOCTEXT("GUID Fixer", "GUID Fixer"));
		Section.AddMenuEntryWithCommandList(FGuidFixerCommands::Get().FixMaterialGuids, FixMaterialGuidsCommands);
		Section.AddMenuEntryWithCommandList(FGuidFixerCommands::Get().FixTextureGuids, FixTextureGuidsCommands);
		Section.AddMenuEntryWithCommandList(FGuidFixerCommands::Get().FixEmptyTextureGuids, FixEmptyTextureGuidsCommands);
	}
}

template<typename T>
bool FGuidFixerModule::ShouldModify(T* Object) const
{
	const bool bIsEngineContent = Object->GetPathName().StartsWith("/Engine/");
	const bool bIsProjectContent = Object->GetPathName().StartsWith("/Game/");
	const bool bIsPluginContent = !bIsEngineContent && !bIsProjectContent;

	return bIsProjectContent;
}

// This function is a modified version of laggyluk's SwarmGuidFixer
// https://github.com/laggyluk/SwarmGuidFixer
void FGuidFixerModule::FixMaterialGuids() const
{
	TMap<FGuid, UMaterialInterface*> Guids;

	bool bMadeChanges = false;
	bool bHasWarnings = false;
	for (TObjectIterator<UMaterialInterface> Material; Material; ++Material)
	{
		if (!Material->GetLightingGuid().IsValid())
		{
			if (ShouldModify(*Material))
			{
				Material->SetLightingGuid();
				Material->Modify();
				bMadeChanges = true;
				UE_LOG(LogTemp, Display, TEXT("%s: Material has had its GUID updated."), *Material->GetPathName());
			}
			else
			{
				bHasWarnings = true;
				UE_LOG(LogTemp, Warning, TEXT("%s: Material has invalid GUID but is specified not to be modified. @see FGuidFixerModule::ShouldModify()"), *Material->GetPathName());
				continue;
			}
		}

		const FGuid CacheCurrentGuid = Material->GetLightingGuid();
		UMaterialInterface** Result = Guids.Find(CacheCurrentGuid);
		if (Result)
		{
			UMaterialInterface* ResultRef = (*Result);

			bool bAnyGuidsModified = false;

			// Update the initial material with the same GUID if its GUID hasn't been updated already
			// This is probably unnecessary, but better safe than sorry
			if (ResultRef->GetLightingGuid() == Material->GetLightingGuid())
			{
				if (ShouldModify(ResultRef))
				{
					ResultRef->SetLightingGuid();
					ResultRef->Modify();
					bAnyGuidsModified = true;
					Guids.Remove(CacheCurrentGuid);
					Guids.Add(ResultRef->GetLightingGuid(), ResultRef);
					UE_LOG(LogTemp, Display, TEXT("%s: Material has had its GUID updated."), *ResultRef->GetPathName());
				}
			}

			if (ShouldModify(*Material))
			{
				Material->SetLightingGuid();
				Material->Modify();
				bMadeChanges = true;
				bAnyGuidsModified = true;
				Guids.Add(Material->GetLightingGuid(), *Material);
				UE_LOG(LogTemp, Display, TEXT("%s: Material has had its GUID updated."), *Material->GetPathName());
			}

			if (!bAnyGuidsModified)
			{
				bHasWarnings = true;
				UE_LOG(LogTemp, Warning, TEXT("%s: Material has conflicting GUID with %s but both are specified not to be modified. @see FGuidFixerModule::ShouldModify()"), *Material->GetPathName(), *ResultRef->GetPathName());
			}

			continue;
		}

		Guids.Add(Material->GetLightingGuid(), *Material);
	}

	FText DialogText = FText::FromString("No duplicate material GUIDs found.");
	if (bMadeChanges && bHasWarnings)
		DialogText = FText::FromString("At least one material GUID has been changed, but there are some unresolvable issues (Please refer to log). Use save all to save these changes.");
	else if (bMadeChanges)
		DialogText = FText::FromString("At least one material GUID has been changed. Use save all to save these changes.");
	else if (bHasWarnings)
		DialogText = FText::FromString("No material GUID has been changed, but there are some unresolvable issues (Please refer to log).");
	FMessageDialog::Open(EAppMsgType::Ok, DialogText);
}

void FGuidFixerModule::FixTextureGuids() const
{
	TMap<FGuid, UTexture*> Guids;

	bool bMadeChanges = false;
	bool bHasWarnings = false;
	for (TObjectIterator<UTexture> Texture; Texture; ++Texture)
	{
		if (!Texture->GetLightingGuid().IsValid())
		{
			bHasWarnings = true;
			UE_LOG(LogTemp, Warning, TEXT("%s: Texture has invalid GUID but is not modified. Fix this by running Tools -> GUID Fixer -> Fix Empty Texture Guids"), *Texture->GetPathName());
			continue;
		}

		const FGuid CacheCurrentGuid = Texture->GetLightingGuid();
		UTexture** Result = Guids.Find(CacheCurrentGuid);
		if (Result)
		{
			UTexture* ResultRef = (*Result);

			bool bAnyGuidsModified = false;

			// Update the initial texture with the same GUID if its GUID hasn't been updated already
			// This is probably unnecessary, but better safe than sorry
			if (ResultRef->GetLightingGuid() == Texture->GetLightingGuid())
			{
				if (ShouldModify(ResultRef))
				{
					ResultRef->SetLightingGuid();
					ResultRef->Modify();
					bAnyGuidsModified = true;
					Guids.Remove(CacheCurrentGuid);
					Guids.Add(ResultRef->GetLightingGuid(), ResultRef);
					UE_LOG(LogTemp, Display, TEXT("%s: Texture has had its GUID updated."), *ResultRef->GetPathName());
				}
			}

			if (ShouldModify(*Texture))
			{
				Texture->SetLightingGuid();
				Texture->Modify();
				bMadeChanges = true;
				bAnyGuidsModified = true;
				Guids.Add(Texture->GetLightingGuid(), *Texture);
				UE_LOG(LogTemp, Display, TEXT("%s: Texture has had its GUID updated."), *Texture->GetPathName());
			}

			if (!bAnyGuidsModified)
			{
				bHasWarnings = true;
				UE_LOG(LogTemp, Warning, TEXT("%s: Texture has conflicting GUID with %s but both are specified not to be modified. @see FGuidFixerModule::ShouldModify()"), *Texture->GetPathName(), *ResultRef->GetPathName());
			}

			continue;
		}

		Guids.Add(Texture->GetLightingGuid(), *Texture);
	}

	FText DialogText = FText::FromString("No duplicate texture GUIDs found.");
	if (bMadeChanges && bHasWarnings)
		DialogText = FText::FromString("At least one texture GUID has been changed, but there are some unresolvable issues (Please refer to log). Use save all to save these changes.");
	else if (bMadeChanges)
		DialogText = FText::FromString("At least one texture GUID has been changed. Use save all to save these changes.");
	else if (bHasWarnings)
		DialogText = FText::FromString("No texture GUID has been changed, but there are some unresolvable issues (Please refer to log).");
	FMessageDialog::Open(EAppMsgType::Ok, DialogText);
}

void FGuidFixerModule::FixEmptyTextureGuids() const
{
	bool bMadeChanges = false;
	bool bHasWarnings = false;
	for (TObjectIterator<UTexture> Texture; Texture; ++Texture)
	{
		if (Texture->GetLightingGuid().IsValid())
		{
			continue;
		}
		
		if (ShouldModify(*Texture))
		{
			Texture->SetLightingGuid();
			Texture->Modify();
			bMadeChanges = true;
			UE_LOG(LogTemp, Display, TEXT("%s: Texture has had its GUID updated."), *Texture->GetPathName());
		}
		else
		{
			bHasWarnings = true;
			UE_LOG(LogTemp, Warning, TEXT("%s: Texture has invalid GUID but is specified not to be modified. @see FGuidFixerModule::ShouldModify()"), *Texture->GetPathName());
		}
	}

	FText DialogText = FText::FromString("No empty texture GUIDs found.");
	if (bMadeChanges && bHasWarnings)
		DialogText = FText::FromString("At least one texture GUID has been changed, but there are some unresolvable issues (Please refer to log). Use save all to save these changes.");
	else if (bMadeChanges)
		DialogText = FText::FromString("At least one texture GUID has been changed. Use save all to save these changes.");
	else if (bHasWarnings)
		DialogText = FText::FromString("No texture GUID has been changed, but there are some unresolvable issues (Please refer to log).");
	FMessageDialog::Open(EAppMsgType::Ok, DialogText);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGuidFixerModule, GuidFixer)
