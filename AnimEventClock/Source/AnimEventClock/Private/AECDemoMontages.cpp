// P06 AnimEventClock (demo) - the authored content of the demo montages.

#include "AECDemoMontages.h"

const TArray<AECDemo::FMontageSpec>& AECDemo::GetMontageSpecs()
{
	// Positions avoid section boundaries and the very start and end, so no marker sits exactly on a segment edge.
	static const TArray<FMontageSpec> Specs = {
		{
			TEXT("AM_Attack"),
			{ { TEXT("Default"), 0.f } },
			{
				{ TEXT("Wind"),      0.12f, 0.f },
				{ TEXT("HitWindow"), 0.30f, 0.32f },   // a window: open 0.30 -> 0.62
				{ TEXT("Hit1"),      0.36f, 0.f },
				{ TEXT("Hit2"),      0.56f, 0.f },
				{ TEXT("Recover"),   0.88f, 0.f },
			}
		},
		{
			TEXT("AM_Combo"),
			{ { TEXT("A"), 0.f }, { TEXT("B"), 1.f / 3.f }, { TEXT("C"), 2.f / 3.f } },
			{
				{ TEXT("A1"),          0.10f, 0.f },
				{ TEXT("ComboWindow"), 0.25f, 0.17f },   // crosses the A -> B boundary: 0.25 -> 0.42
				{ TEXT("B1"),          0.45f, 0.f },
				{ TEXT("C1"),          0.78f, 0.f },
				{ TEXT("End"),         0.94f, 0.f },
			}
		},
	};
	return Specs;
}

FString AECDemo::GetMontageAssetName(const FMontageSpec& Spec, bool bBranchingPoint)
{
	return FString::Printf(TEXT("%s_%s"), Spec.BaseName, bBranchingPoint ? TEXT("B") : TEXT("Q"));
}

FString AECDemo::GetMontagePackageName(const FMontageSpec& Spec, bool bBranchingPoint)
{
	return FString::Printf(TEXT("%s/%s"), MontageFolder, *GetMontageAssetName(Spec, bBranchingPoint));
}

FString AECDemo::GetMontageObjectPath(const FMontageSpec& Spec, bool bBranchingPoint)
{
	const FString AssetName = GetMontageAssetName(Spec, bBranchingPoint);
	return FString::Printf(TEXT("%s/%s.%s"), MontageFolder, *AssetName, *AssetName);
}
