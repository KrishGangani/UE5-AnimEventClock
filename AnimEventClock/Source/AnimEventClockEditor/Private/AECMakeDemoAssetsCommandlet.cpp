// P06 AnimEventClock (editor) - UAECMakeDemoAssetsCommandlet.

#include "AECMakeDemoAssetsCommandlet.h"

#include "AECDemoMontages.h"
#include "AECEventNotify.h"
#include "AECEventWindowNotifyState.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "AnimationBlueprintLibrary.h"
#include "Factories/AnimMontageFactory.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AECMakeDemoAssetsCommandlet)

DEFINE_LOG_CATEGORY_STATIC(LogAECAssets, Log, All);

namespace AECMakeDemoAssets
{
	/** Builds, checks and saves one montage. Returns false (and logs why) on any failure. */
	bool MakeMontage(UAnimSequence& Source, const AECDemo::FMontageSpec& Spec, bool bBranchingPoint);
}

int32 UAECMakeDemoAssetsCommandlet::Main(const FString& Params)
{
	UAnimSequence* Source = LoadObject<UAnimSequence>(nullptr, AECDemo::SourceSequencePath);
	if (!Source)
	{
		UE_LOG(LogAECAssets, Error, TEXT("Source sequence not found: %s"), AECDemo::SourceSequencePath);
		return 1;
	}
	UE_LOG(LogAECAssets, Display, TEXT("Source %s: %.4f s"), *Source->GetPathName(), Source->GetPlayLength());

	bool bAllSaved = true;
	for (const AECDemo::FMontageSpec& Spec : AECDemo::GetMontageSpecs())
	{
		for (const bool bBranchingPoint : { false, true })
		{
			if (!AECMakeDemoAssets::MakeMontage(*Source, Spec, bBranchingPoint))
			{
				bAllSaved = false;
			}
		}
	}
	return bAllSaved ? 0 : 1;
}

bool AECMakeDemoAssets::MakeMontage(UAnimSequence& Source, const AECDemo::FMontageSpec& Spec, bool bBranchingPoint)
{
	const FString AssetName = AECDemo::GetMontageAssetName(Spec, bBranchingPoint);
	const FString PackageName = AECDemo::GetMontagePackageName(Spec, bBranchingPoint);
	UPackage* Package = CreatePackage(*PackageName);

	// The editor's own "Create Montage" path: one slot track holding the source sequence, the montage length set from
	// it, and a first section "Default" at 0. Called through UFactory, whose FactoryCreateNew is the exported virtual.
	UAnimMontageFactory* MontageFactory = NewObject<UAnimMontageFactory>();
	MontageFactory->SourceAnimation = &Source;
	UFactory* Factory = MontageFactory;
	UAnimMontage* Montage = Cast<UAnimMontage>(Factory->FactoryCreateNew(UAnimMontage::StaticClass(), Package, FName(*AssetName),
		RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
	if (!Montage || Spec.Sections.IsEmpty())
	{
		UE_LOG(LogAECAssets, Error, TEXT("%s: the montage factory failed (or the spec has no sections)."), *AssetName);
		return false;
	}
	const float Length = Montage->GetPlayLength();

	// Sections: rename the factory's first one, then add the rest. AddAnimCompositeSection links the previous section
	// to the new one when the previous has no link yet, so A -> B -> C is linked in array order.
	Montage->CompositeSections[0].SectionName = FName(Spec.Sections[0].Name);
	for (int32 Index = 1; Index < Spec.Sections.Num(); ++Index)
	{
		if (Montage->AddAnimCompositeSection(FName(Spec.Sections[Index].Name), Spec.Sections[Index].StartFraction * Length) == INDEX_NONE)
		{
			UE_LOG(LogAECAssets, Error, TEXT("%s: could not add section %s."), *AssetName, Spec.Sections[Index].Name);
			return false;
		}
	}

	// Markers, through the same library the editor's scripting uses (it links each notify to the montage, sets the
	// trigger offset the editor would, and refreshes the montage's cached data).
	const FName TrackName(AECDemo::NotifyTrackName);
	UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Montage, TrackName);
	for (const AECDemo::FMarkerSpec& Marker : Spec.Markers)
	{
		const float StartTime = Marker.StartFraction * Length;
		if (Marker.DurationFraction <= 0.f)
		{
			UAECEventNotify* Event = Cast<UAECEventNotify>(
				UAnimationBlueprintLibrary::AddAnimationNotifyEvent(Montage, TrackName, StartTime, UAECEventNotify::StaticClass()));
			if (!Event)
			{
				UE_LOG(LogAECAssets, Error, TEXT("%s: could not add event %s at %.4f s."), *AssetName, Marker.Name, StartTime);
				return false;
			}
			Event->EventName = FName(Marker.Name);
		}
		else
		{
			UAECEventWindowNotifyState* Window = Cast<UAECEventWindowNotifyState>(UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(
				Montage, TrackName, StartTime, Marker.DurationFraction * Length, UAECEventWindowNotifyState::StaticClass()));
			if (!Window)
			{
				UE_LOG(LogAECAssets, Error, TEXT("%s: could not add window %s at %.4f s."), *AssetName, Marker.Name, StartTime);
				return false;
			}
			Window->EventName = FName(Marker.Name);
		}
	}

	// The tick type is a property of each notify event, not of the notify class. The display name was taken from the
	// class before EventName was set, so it is refreshed here too.
	for (FAnimNotifyEvent& Event : Montage->Notifies)
	{
		Event.MontageTickType = bBranchingPoint ? EMontageNotifyTickType::BranchingPoint : EMontageNotifyTickType::Queued;
		if (Event.Notify)
		{
			Event.NotifyName = FName(*Event.Notify->GetNotifyName());
		}
		else if (Event.NotifyStateClass)
		{
			Event.NotifyName = FName(*Event.NotifyStateClass->GetNotifyName());
		}
	}
	Montage->RefreshCacheData();   // rebuilds the branching-point markers from the new tick types

	if (Montage->Notifies.Num() != Spec.Markers.Num())
	{
		UE_LOG(LogAECAssets, Error, TEXT("%s: %d notifies, expected %d."), *AssetName, Montage->Notifies.Num(), Spec.Markers.Num());
		return false;
	}

	Package->MarkPackageDirty();
	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GWarn;
	if (!UPackage::SavePackage(Package, Montage, *Filename, SaveArgs))
	{
		UE_LOG(LogAECAssets, Error, TEXT("%s: saving %s failed."), *AssetName, *Filename);
		return false;
	}

	FString Summary;
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		Summary += FString::Printf(TEXT(" %s@%.4f"), *Event.NotifyName.ToString(), Event.GetTriggerTime());
	}
	UE_LOG(LogAECAssets, Display, TEXT("Saved %s: %.4f s, %d section(s), %d marker(s), %s:%s"), *PackageName, Length,
		Montage->CompositeSections.Num(), Montage->Notifies.Num(), bBranchingPoint ? TEXT("BranchingPoint") : TEXT("Queued"), *Summary);
	return true;
}
