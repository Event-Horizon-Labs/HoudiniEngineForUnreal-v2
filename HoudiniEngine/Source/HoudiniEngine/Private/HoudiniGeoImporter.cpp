/*
* Copyright (c) <2018> Side Effects Software Inc.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*
* 2. The name of Side Effects Software may not be used to endorse or
*    promote products derived from this software without specific prior
*    written permission.
*
* THIS SOFTWARE IS PROVIDED BY SIDE EFFECTS SOFTWARE "AS IS" AND ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
* NO EVENT SHALL SIDE EFFECTS SOFTWARE BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "HoudiniGeoImporter.h"

#include "HoudiniApi.h"
#include "HoudiniEngine.h"
#include "HoudiniEngineUtils.h"
#include "HoudiniEngineRuntime.h"
#include "HoudiniEnginePrivatePCH.h"
#include "HoudiniPackageParams.h"
#include "HoudiniOutput.h"
#include "HoudiniOutputTranslator.h"
#include "HoudiniMeshTranslator.h"
#include "HoudiniLandscapeTranslator.h"
#include "HoudiniInstanceTranslator.h"

#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "PackageTools.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"

UHoudiniGeoImporter::UHoudiniGeoImporter(const FObjectInitializer & ObjectInitializer)
	: Super(ObjectInitializer)
	, SourceFilePath()
	, AbsoluteFilePath()
	, AbsoluteFileDirectory()
	, FileName()
	, FileExtension()
	, BakeRootFolder(TEXT("/Game/HoudiniEngine/Bake/"))
{
	/*
	SourceFilePath = FString();

	AbsoluteFilePath = FString();
	AbsoluteFileDirectory = FString();
	FileName = FString();
	FileExtension = FString();

	OutputFilename = FString();
	BakeRootFolder = TEXT("/Game/HoudiniEngine/Bake/");
	*/
}

bool
UHoudiniGeoImporter::SetFilePath(const FString& InFilePath)
{
	SourceFilePath = InFilePath;
	if (!FPaths::FileExists(SourceFilePath))
	{
		// Cant find BGEO file
		HOUDINI_LOG_ERROR(TEXT("Houdini GEO Importer: could not find file %s!"), *InFilePath);
		return false;
	}

	// Make sure we're using absolute path!
	AbsoluteFilePath = FPaths::ConvertRelativePathToFull(SourceFilePath);

	// Split the file path
	FPaths::Split(AbsoluteFilePath, AbsoluteFileDirectory, FileName, FileExtension);

	// Handle .bgeo.sc correctly
	if (FileExtension.Equals(TEXT("sc")))
	{
		// append the bgeo to .sc
		FileExtension = FPaths::GetExtension(FileName) +TEXT(".") + FileExtension;
		// update the filename
		FileName = FPaths::GetBaseFilename(FileName);
	}

	if (FileExtension.IsEmpty())
		FileExtension = TEXT("bgeo");

	if (!FileExtension.StartsWith(TEXT("bgeo"), ESearchCase::IgnoreCase))
	{
		// Not a bgeo file!
		HOUDINI_LOG_ERROR(TEXT("Houdini GEO Importer: File %s is not a .bgeo or .bgeo.sc file!"), *SourceFilePath);
		return false;
	}

	//BGEOFilePath = BGEOPath + TEXT("/") + BGEOFileName + TEXT(".") + BGEOExtension;

	// Only use "/" for the output file path
	BakeRootFolder.ReplaceInline(TEXT("\\"), TEXT("/"));
	// Make sure the output folder ends with a "/"
	if (!BakeRootFolder.EndsWith("/"))
		BakeRootFolder += TEXT("/");

	// If we have't specified an outpout file name yet,  use the input file name
	if (OutputFilename.IsEmpty())
		OutputFilename = FileName;

	return true;
}

bool
UHoudiniGeoImporter::AutoStartHoudiniEngineSessionIfNeeded()
{
	if (FHoudiniEngine::Get().GetSession())
		return true;

	// Default first session already attempted to be created ? stop here?
	/*
	if (FHoudiniEngine::Get().GetFirstSessionCreated())
		return false;
	*/

	// Indicates that we've tried to start a session once no matter if it failed or succeed
	FHoudiniEngine::Get().SetFirstSessionCreated(true);
	if (!FHoudiniEngine::Get().RestartSession())
	{
		HOUDINI_LOG_ERROR(TEXT("Houdini GEO Importer: Couldn't start the default HoudiniEngine session!"));
		return false;
	}

	return true;
}

bool
UHoudiniGeoImporter::BuildOutputsForNode(const HAPI_NodeId& InNodeId, TArray<UHoudiniOutput*>& OutNewOutputs, bool& bUseWorldComposition)
{
	FString Notification = TEXT("BGEO Importer: Getting output geos...");
	FHoudiniEngine::Get().UpdateTaskSlateNotification(FText::FromString(Notification));

	//
	bUseWorldComposition = false;
	TArray<UHoudiniOutput*> OldOutputs;
	if (!FHoudiniOutputTranslator::BuildAllOutputs(InNodeId, this, OldOutputs, OutNewOutputs, bUseWorldComposition, true))
	{
		// Couldn't create the package
		HOUDINI_LOG_ERROR(TEXT("Houdini GEO Importer: Failed to process the File SOP's outputs!"));
		return false;
	}

	// Add the output objects to the RootSet to prevent them from being GCed
	for (auto& Out : OutNewOutputs)
		Out->AddToRoot();

	return true;
}

bool
UHoudiniGeoImporter::CreateStaticMeshes(TArray<UHoudiniOutput*>& InOutputs, UObject* InParent, FHoudiniPackageParams InPackageParams)
{
	for (auto& CurOutput : InOutputs)
	{
		if (CurOutput->GetType() != EHoudiniOutputType::Mesh)
			continue;

		FString Notification = TEXT("BGEO Importer: Creating Static Meshes...");
		FHoudiniEngine::Get().UpdateTaskSlateNotification(FText::FromString(Notification));

		//FHoudiniMeshTranslator::CreateAllMeshesFromHoudiniOutput(CurOutput, OuterPackage, OuterComponent, OuterAsset);

		TMap<FHoudiniOutputObjectIdentifier, FHoudiniOutputObject> NewOutputObjects;
		TMap<FHoudiniOutputObjectIdentifier, FHoudiniOutputObject> OldOutputObjects = CurOutput->GetOutputObjects();
		TMap<FString, UMaterialInterface*>& AssignementMaterials = CurOutput->GetAssignementMaterials();
		TMap<FString, UMaterialInterface*>& ReplacementMaterials = CurOutput->GetReplacementMaterials();

		// Iterate on all of the output's HGPO, creating meshes as we go
		for (const FHoudiniGeoPartObject& CurHGPO : CurOutput->GetHoudiniGeoPartObjects())
		{
			// Not a mesh, skip
			if (CurHGPO.Type != EHoudiniPartType::Mesh)
				continue;

			FHoudiniMeshTranslator::CreateStaticMeshFromHoudiniGeoPartObject(
				CurHGPO,
				InPackageParams,
				OldOutputObjects,
				NewOutputObjects,
				AssignementMaterials,
				ReplacementMaterials,
				true,
				EHoudiniStaticMeshMethod::RawMesh);
		}

		// Add all output objects and materials
		for (auto CurOutputPair : NewOutputObjects)
		{
			UObject* CurObj = CurOutputPair.Value.OutputObject;
			if (!CurObj || CurObj->IsPendingKill())
				continue;

			OutputObjects.Add(CurObj);
		}

		// Do the same for materials
		for (auto CurAssignmentMatPair : AssignementMaterials)
		{
			UObject* CurObj = CurAssignmentMatPair.Value;
			if (!CurObj || CurObj->IsPendingKill())
				continue;

			OutputObjects.Add(CurObj);
		}

		// Also assign to the output objects map as we may need the meshes to create instancers later
		CurOutput->SetOutputObjects(NewOutputObjects);
	}

	return true;
}


bool
UHoudiniGeoImporter::CreateLandscapes(TArray<UHoudiniOutput*>& InOutputs, UObject* InParent, FHoudiniPackageParams InPackageParams)
{
	// Before processing any of the output,
	// we need to get the min/max value for all Height volumes in this output (if any)
	float HoudiniHeightfieldOutputsGlobalMin = 0.f;
	float HoudiniHeightfieldOutputsGlobalMax = 0.f;
	FHoudiniLandscapeTranslator::CalcHeightGlobalZminZMax(InOutputs, HoudiniHeightfieldOutputsGlobalMin, HoudiniHeightfieldOutputsGlobalMax);


	TArray<ALandscapeProxy*> DummyValidLandscapes;
	TArray<ALandscapeProxy*> DummyInputLandscapesToUpdate;
	bool bUseWorldComposition = true;
	for (auto& CurOutput : InOutputs)
	{
		if (CurOutput->GetType() != EHoudiniOutputType::Landscape)
			continue;

		FString Notification = TEXT("BGEO Importer: Creating Landscapes...");
		FHoudiniEngine::Get().UpdateTaskSlateNotification(FText::FromString(Notification));

		/*
		PackageParams.ObjectName = FPaths::GetBaseFilename(InParent->GetName());
		*/
		CurOutput->SetLandscapeWorldComposition(bUseWorldComposition);

		FHoudiniLandscapeTranslator::CreateAllLandscapesFromHoudiniOutput(
			CurOutput, DummyInputLandscapesToUpdate, DummyValidLandscapes,
			HoudiniHeightfieldOutputsGlobalMin, HoudiniHeightfieldOutputsGlobalMax,
			bUseWorldComposition, InPackageParams);

		// Add all output objects
		for (auto CurOutputPair : CurOutput->GetOutputObjects())
		{
			UObject* CurObj = CurOutputPair.Value.OutputObject;
			if (!CurObj || CurObj->IsPendingKill())
				continue;

			OutputObjects.Add(CurObj);
		}
	}

	return true;
}


bool
UHoudiniGeoImporter::CreateInstancers(TArray<UHoudiniOutput*>& InOutputs, UObject* InParent, FHoudiniPackageParams InPackageParams)
{
	bool HasInstancer = false;
	for (auto& CurOutput : InOutputs)
	{
		if (CurOutput->GetType() != EHoudiniOutputType::Instancer)
			continue;

		HasInstancer = true;
		break;
	}

	if (!HasInstancer)
		return true;

	FString Notification = TEXT("BGEO Importer: Creating Instancers...");
	FHoudiniEngine::Get().UpdateTaskSlateNotification(FText::FromString(Notification));

	// Create a Package for the BP
	InPackageParams.ObjectName = TEXT("BP_") + InPackageParams.ObjectName;
	InPackageParams.ReplaceMode = EPackageReplaceMode::CreateNewAssets;
	
	FString PackageName;
	UPackage* BPPackage = InPackageParams.CreatePackageForObject(PackageName);
	check(BPPackage);

	// Create and init a new Blueprint Actor
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), BPPackage, *PackageName, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName("HoudiniGeoImporter"));
	if (!Blueprint)
		return false;

	// Create a fake outer component that we'll use as a temporary outer for our instancers
	USceneComponent* OuterComponent = NewObject<USceneComponent>();

	bool bUseWorldComposition = true;
	for (auto& CurOutput : InOutputs)
	{
		if (CurOutput->GetType() != EHoudiniOutputType::Instancer)
			continue;

		// Create all the instancers and attach them to a fake outer component
		FHoudiniInstanceTranslator::CreateAllInstancersFromHoudiniOutput(
			CurOutput, InOutputs, OuterComponent);

		// Prepare an ActorComponent array for AddComponentsToBlueprint()
		TArray<UActorComponent*> OutputComp;
		for (auto CurOutputPair : CurOutput->GetOutputObjects())
		{
			UActorComponent* CurObj = Cast<UActorComponent>(CurOutputPair.Value.OutputComponent);
			if (!CurObj || CurObj->IsPendingKill())
				continue;

			OutputComp.Add(CurObj);
		}

		// Transfer all the instancer components to the BP
		if (OutputComp.Num() > 0)
		{
			FKismetEditorUtilities::AddComponentsToBlueprint(Blueprint, OutputComp, false, nullptr, false);
		}
	}

	// Compile the blueprint
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	// Add it to our output objects
	OutputObjects.Add(Blueprint);

	return true;
}

bool
UHoudiniGeoImporter::DeleteCreatedNode(const HAPI_NodeId& InNodeId)
{
	if (InNodeId < 0)
		return false;

	if (HAPI_RESULT_SUCCESS != FHoudiniApi::DeleteNode(FHoudiniEngine::Get().GetSession(), InNodeId))
	{
		// Could not delete the bgeo's file sop !
		HOUDINI_LOG_WARNING(TEXT("Houdini GEO Importer: Could not delete HAPI File SOP for %s"), *SourceFilePath);
		return false;
	}

	return true;
}

bool 
UHoudiniGeoImporter::ImportBGEOFile(const FString& InBGEOFile, UObject* InParent)
{
	if (InBGEOFile.IsEmpty())
		return false;
	
	// 1. Houdini Engine Session
	// See if we should/can start the default "first" HE session
	if (!AutoStartHoudiniEngineSessionIfNeeded())
		return false;

	// 2. Update the file paths
	if (!SetFilePath(InBGEOFile))
		return false;

	// 3. Load the BGEO file in HAPI
	HAPI_NodeId NodeId;
	if (!LoadBGEOFileInHAPI(NodeId))
		return false;
	
	// 4. Get the output from the file node
	bool bUseWorldComposition = false;
	TArray<UHoudiniOutput*> NewOutputs;
	if (!BuildOutputsForNode(NodeId, NewOutputs, bUseWorldComposition))
		return false;

	// Failure lambda
	auto CleanUpAndReturn = [&NewOutputs](const bool& bReturnValue)
	{
		// Remove the output objects from the root set before returning false
		for (auto Out : NewOutputs)
			Out->RemoveFromRoot();

		return bReturnValue;
	};

	// Prepare the package used for creating the mesh, landscape and instancer pacakges
	FHoudiniPackageParams PackageParams;
	PackageParams.PackageMode = EPackageMode::Bake;
	PackageParams.ReplaceMode = EPackageReplaceMode::ReplaceExistingAssets;

	PackageParams.BakeFolder = FPackageName::GetLongPackagePath(InParent->GetOutermost()->GetName());
	PackageParams.TempCookFolder = FHoudiniEngineRuntime::Get().GetDefaultTemporaryCookFolder();

	PackageParams.OuterPackage = InParent;
	PackageParams.HoudiniAssetName = FString();
	PackageParams.ObjectName = FPaths::GetBaseFilename(InParent->GetName());

	// TODO: will need to reuse the GUID when reimporting?
	PackageParams.ComponentGUID = FGuid::NewGuid();

	// 5. Create the static meshes in the outputs
	if (!CreateStaticMeshes(NewOutputs, InParent, PackageParams))
		return CleanUpAndReturn(false);

	// 6. Create the landscape in the outputs
	if (!CreateLandscapes(NewOutputs, InParent, PackageParams))
		return CleanUpAndReturn(false);

	// 7. Create the instancers in the outputs
	if (!CreateInstancers(NewOutputs, InParent, PackageParams))
		return CleanUpAndReturn(false);

	// 8. Delete the created  node in Houdini
	if (!DeleteCreatedNode(NodeId))
		return CleanUpAndReturn(false);
	
	// Clean up and return true
	return CleanUpAndReturn(true);
}


bool
UHoudiniGeoImporter::LoadBGEOFileInHAPI(HAPI_NodeId& NodeId)
{
	NodeId = -1;

	if (AbsoluteFilePath.IsEmpty())
		return false;

	// Check HoudiniEngine / HAPI init?
	if (!FHoudiniEngine::IsInitialized())
	{
		HOUDINI_LOG_ERROR(TEXT("Couldn't initialize HoudiniEngine!"));
		return false;
	}

	FString Notification = TEXT("BGEO Importer: Loading bgeo file...");
	FHoudiniEngine::Get().CreateTaskSlateNotification(FText::FromString(Notification), true);

	// Create a file SOP
	HOUDINI_CHECK_ERROR_RETURN( FHoudiniEngineUtils::CreateNode(
		-1,	"SOP/file", "bgeo", true, &NodeId), false);

	// Set the file path parameter
	HAPI_ParmId ParmId = -1;
	HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::GetParmIdFromName(
		FHoudiniEngine::Get().GetSession(),
		NodeId, "file", &ParmId), false);

	std::string ConvertedString = TCHAR_TO_UTF8(*AbsoluteFilePath);
	HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::SetParmStringValue(
		FHoudiniEngine::Get().GetSession(), NodeId, ConvertedString.c_str(), ParmId, 0), false);

	// Cook the node    
	HAPI_CookOptions CookOptions;
	FHoudiniApi::CookOptions_Init(&CookOptions);
	//FMemory::Memzero< HAPI_CookOptions >( CookOptions );
	CookOptions.curveRefineLOD = 8.0f;
	CookOptions.clearErrorsAndWarnings = false;
	CookOptions.maxVerticesPerPrimitive = 3;
	CookOptions.splitGeosByGroup = false;
	CookOptions.splitGeosByAttribute = false;
	CookOptions.splitAttrSH = 0;
	CookOptions.refineCurveToLinear = true;
	CookOptions.handleBoxPartTypes = false;
	CookOptions.handleSpherePartTypes = false;
	CookOptions.splitPointsByVertexAttributes = false;
	CookOptions.packedPrimInstancingMode = HAPI_PACKEDPRIM_INSTANCING_MODE_FLAT;
	HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::CookNode(
		FHoudiniEngine::Get().GetSession(), NodeId, &CookOptions), false);

	// Wait for the cook to finish
	int32 status = HAPI_STATE_MAX_READY_STATE + 1;
	while (status > HAPI_STATE_MAX_READY_STATE)
	{
		// Retrieve the status
		HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::GetStatus(
			FHoudiniEngine::Get().GetSession(),
			HAPI_STATUS_COOK_STATE, &status), false);

		FString StatusString = FHoudiniEngineUtils::GetStatusString(HAPI_STATUS_COOK_STATE, HAPI_STATUSVERBOSITY_ERRORS);
		HOUDINI_LOG_MESSAGE(TEXT("Still Cooking, current status: %s."), *StatusString);

		// Go to bed..
		if (status > HAPI_STATE_MAX_READY_STATE)
			FPlatformProcess::Sleep(0.5f);
	}

	if (status != HAPI_STATE_READY)
	{
		// There was some cook errors
		HOUDINI_LOG_ERROR(TEXT("Finished Cooking with errors!"));
		return false;
	}

	HOUDINI_LOG_MESSAGE(TEXT("Finished Cooking!"));

	return true;
}
