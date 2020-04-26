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

#include "HoudiniInstanceTranslator.h"

#include "HoudiniEngine.h"
#include "HoudiniEngineUtils.h"
#include "HoudiniEnginePrivatePCH.h"
#include "HoudiniGenericAttribute.h"
#include "HoudiniInstancedActorComponent.h"
#include "HoudiniMeshSplitInstancerComponent.h"
#include "HoudiniStaticMeshComponent.h"
#include "HoudiniStaticMesh.h"

//#include "HAPI/HAPI_Common.h"

#include "Engine/StaticMesh.h"
#include "ComponentReregisterContext.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

#if WITH_EDITOR
	//#include "ScopedTransaction.h"
	#include "LevelEditorViewport.h"
	#include "MeshPaintHelpers.h"
#endif

#define LOCTEXT_NAMESPACE HOUDINI_LOCTEXT_NAMESPACE

// Fastrand is a faster alternative to std::rand()
// and doesn't oscillate when looking for 2 values like Unreal's.
inline int fastrand(int& nSeed)
{
	nSeed = (214013 * nSeed + 2531011);
	return (nSeed >> 16) & 0x7FFF;
}

// 
bool
FHoudiniInstanceTranslator::CreateAllInstancersFromHoudiniOutput(
	UHoudiniOutput* InOutput,
	const TArray<UHoudiniOutput*>& InAllOutputs,
	UObject* InOuterComponent)
{
	if (!InOutput || InOutput->IsPendingKill())
		return false;

	if (!InOuterComponent || InOuterComponent->IsPendingKill())
		return false;

	// Keep track of the previous cook's component to clean them up after
	TMap<FHoudiniOutputObjectIdentifier, FHoudiniOutputObject> NewOutputObjects;
	TMap<FHoudiniOutputObjectIdentifier, FHoudiniOutputObject> OldOutputObjects = InOutput->GetOutputObjects();

	TMap<FHoudiniOutputObjectIdentifier, FHoudiniInstancedOutput>& InstancedOutputs = InOutput->GetInstancedOutputs();
	// Mark all the current instanced output as stale
	for (auto& InstOut : InstancedOutputs)
		InstOut.Value.bStale = true;

	USceneComponent* ParentComponent = Cast<USceneComponent>(InOuterComponent);
	if (!ParentComponent)
		return false;

	// Iterate on all of the output's HGPO, creating meshes as we go
	for (const FHoudiniGeoPartObject& CurHGPO : InOutput->HoudiniGeoPartObjects)
	{
		// Not an instancer, skip
		if (CurHGPO.Type != EHoudiniPartType::Instancer)
			continue;

		// Prepare this output object's output identifier
		FHoudiniOutputObjectIdentifier OutputIdentifier;
		OutputIdentifier.ObjectId = CurHGPO.ObjectId;
		OutputIdentifier.GeoId = CurHGPO.GeoId;
		OutputIdentifier.PartId = CurHGPO.PartId;
		OutputIdentifier.PartName = CurHGPO.PartName;

		// Extract the object and transforms for this instancer
		TArray<UObject*> OriginalInstancedObjects;
		TArray<TArray<FTransform>> OriginalInstancedTransforms;
		if (!GetInstancerObjectsAndTransforms(CurHGPO, InAllOutputs, OriginalInstancedObjects, OriginalInstancedTransforms))
			continue;
		
		//
		// TODO: REFACTOR THIS!
		//
		// We create an instanced output per original object 
		// These original object can then potentially be replaced by variations
		// Each variations will create a instance component / OutputObject
		// Currently we process all original objects AND their variations at the same time
		// we should instead loop on the original objects
		//	- get their variations objects/transform 
		//  - create the appropriate instancer
		// This means modifying UpdateInstanceVariationsObjects so that it works using 
		// a single OriginalObject instead of using an array
		// Also, apply the same logic to UpdateChangedInstanceOutput
		//

		// Array containing all the variations objects for all the original objects
		TArray<TSoftObjectPtr<UObject>> VariationInstancedObjects;
		// Array containing all the variations transforms
		TArray<TArray<FTransform>> VariationInstancedTransforms;
		// Array indicate the original object index for each variation
		TArray<int32> VariationOriginalObjectIndices;
		// Array indicate the variation number for each variation
		TArray<int32> VariationIndices;
		// Update our variations using the instanced outputs
		UpdateInstanceVariationObjects(
			OutputIdentifier,
			OriginalInstancedObjects, OriginalInstancedTransforms, InOutput->GetInstancedOutputs(),
			VariationInstancedObjects, VariationInstancedTransforms, 
			VariationOriginalObjectIndices, VariationIndices);

		// Check if this is a No-Instancers ( unreal_split_instances )
		bool bSplitMeshInstancer = IsSplitInstancer(CurHGPO.GeoId, CurHGPO.PartId);

		// Extract the generic attributes
		TArray<FHoudiniGenericAttribute> AllPropertyAttributes;
		GetGenericPropertiesAttributes(OutputIdentifier.GeoId, OutputIdentifier.PartId, AllPropertyAttributes);

		// See if we have instancer material overrides
		TArray<UMaterialInterface*> InstancerMaterials;
		if (!GetInstancerMaterials(OutputIdentifier.GeoId, OutputIdentifier.PartId, InstancerMaterials))
			InstancerMaterials.Empty();

		// Create the instancer components now
		for (int32 InstanceObjectIdx = 0; InstanceObjectIdx < VariationInstancedObjects.Num(); InstanceObjectIdx++)
		{
			UObject* InstancedObject = VariationInstancedObjects[InstanceObjectIdx].LoadSynchronous();
			if (!InstancedObject || InstancedObject->IsPendingKill())
				continue;

			if (!VariationInstancedTransforms.IsValidIndex(InstanceObjectIdx))
				continue;

			const TArray<FTransform>& InstancedObjectTransforms = VariationInstancedTransforms[InstanceObjectIdx];
			if (InstancedObjectTransforms.Num() <= 0)
				continue;

			// Find the matching instance output now
			FHoudiniInstancedOutput* FoundInstancedOutput = nullptr;
			{
				// Instanced output only use the original object index for their split identifier
				FHoudiniOutputObjectIdentifier InstancedOutputIdentifier = OutputIdentifier;
				InstancedOutputIdentifier.SplitIdentifier = FString::FromInt(VariationOriginalObjectIndices[InstanceObjectIdx]);
				FoundInstancedOutput = InstancedOutputs.Find(InstancedOutputIdentifier);
			}

			// Update the split identifier for this object
			// We use both the original object index and the variation index: ORIG_VAR
			OutputIdentifier.SplitIdentifier = 
				FString::FromInt(VariationOriginalObjectIndices[InstanceObjectIdx])
				+ TEXT("_")
				+ FString::FromInt(VariationIndices[InstanceObjectIdx]);
				
			// Get the OutputObj for this variation
			FHoudiniOutputObject* FoundOutputObject = OldOutputObjects.Find(OutputIdentifier);
			// See if we can find an preexisting component for this obj	to try to reuse it
			USceneComponent* OldInstancerComponent = nullptr;
			const bool bIsProxyMesh = InstancedObject->IsA<UHoudiniStaticMesh>();
			if (FoundOutputObject)
			{
				if (bIsProxyMesh)
				{
					OldInstancerComponent = Cast<USceneComponent>(FoundOutputObject->ProxyComponent);
				}
				else
				{
					OldInstancerComponent = Cast<USceneComponent>(FoundOutputObject->OutputComponent);
				}
			}

			// Extract the material for this variation
			TArray<UMaterialInterface*> VariationMaterials;
			if (!GetVariationMaterials(FoundInstancedOutput, InstanceObjectIdx, InstancerMaterials, VariationMaterials))
				VariationMaterials.Empty();

			USceneComponent* NewInstancerComponent = nullptr;
			if (!CreateOrUpdateInstanceComponent(
				InstancedObject, InstancedObjectTransforms, 
				AllPropertyAttributes, CurHGPO,
				ParentComponent, OldInstancerComponent, NewInstancerComponent,
				bSplitMeshInstancer, VariationMaterials))
			{
				// TODO??
				continue;
			}

			if (!NewInstancerComponent)
				continue;

			FHoudiniOutputObject& NewOutputObject = NewOutputObjects.FindOrAdd(OutputIdentifier);
			if (bIsProxyMesh)
			{
				NewOutputObject.ProxyComponent = NewInstancerComponent;
			}
			else
			{
				NewOutputObject.OutputComponent = NewInstancerComponent;
			}
		}
	}

	// Remove reused components from the old map to avoid their deletion
	for (const auto& CurNewPair : NewOutputObjects)
	{
		// Get the new Identifier / StaticMesh
		const FHoudiniOutputObjectIdentifier& OutputIdentifier = CurNewPair.Key;
		
		// See if we already had that pair in the old map
		FHoudiniOutputObject* FoundOldOutputObject = OldOutputObjects.Find(OutputIdentifier);
		if (!FoundOldOutputObject)
			continue;

		bool bKeep = false;

		UObject* NewComponent = CurNewPair.Value.OutputComponent;
		if (NewComponent)
		{
			UObject* FoundOldComponent = FoundOldOutputObject->OutputComponent;
			if (FoundOldComponent && !FoundOldComponent->IsPendingKill())
			{
				bKeep = (FoundOldComponent == NewComponent);
			}
		}

		UObject* NewProxyComponent = CurNewPair.Value.ProxyComponent;
		if (NewProxyComponent)
		{
			UObject* FoundOldProxyComponent = FoundOldOutputObject->ProxyComponent;
			if (FoundOldProxyComponent && !FoundOldProxyComponent->IsPendingKill())
			{
				bKeep = (FoundOldProxyComponent == NewProxyComponent);
			}
		}

		if (bKeep)
		{
			// Remove the reused component from the old map to avoid its destruction
			OldOutputObjects.Remove(OutputIdentifier);
		}
	}

	// The Old map now only contains unused/stale components, delete them
	for (auto& OldPair : OldOutputObjects)
	{
		// Get the old Identifier / StaticMesh
		FHoudiniOutputObjectIdentifier& OutputIdentifier = OldPair.Key;
		UObject* OldComponent = OldPair.Value.OutputComponent;
		if (OldComponent)
		{
			RemoveAndDestroyComponent(OldComponent);
			OldPair.Value.OutputComponent = nullptr;
		}

		UObject* OldProxyComponent = OldPair.Value.ProxyComponent;
		if (OldProxyComponent)
		{
			RemoveAndDestroyComponent(OldProxyComponent);
			OldPair.Value.ProxyComponent = nullptr;
		}
	}
	OldOutputObjects.Empty();

	// Update the output's object map
	// Instancer do not create objects, clean the map
	InOutput->SetOutputObjects(NewOutputObjects);

	return true;
}


bool
FHoudiniInstanceTranslator::UpdateChangedInstancedOutput(
	FHoudiniInstancedOutput& InInstancedOutput,
	const FHoudiniOutputObjectIdentifier& InOutputIdentifier,
	UHoudiniOutput* InParentOutput,
	USceneComponent* InParentComponent)
{
	FHoudiniOutputObjectIdentifier OutputIdentifier;
	OutputIdentifier.ObjectId = InOutputIdentifier.ObjectId;
	OutputIdentifier.GeoId = InOutputIdentifier.GeoId;
	OutputIdentifier.PartId = InOutputIdentifier.PartId;
	OutputIdentifier.SplitIdentifier = InOutputIdentifier.SplitIdentifier;
	OutputIdentifier.PartName = InOutputIdentifier.PartName;

	TArray<UObject*> OriginalInstancedObjects;
	OriginalInstancedObjects.Add(InInstancedOutput.OriginalObject.LoadSynchronous());

	TArray<TArray<FTransform>> OriginalInstancedTransforms;
	OriginalInstancedTransforms.Add(InInstancedOutput.OriginalTransforms);

	// Update our variations using the changed instancedoutputs objects
	TArray<TSoftObjectPtr<UObject>> InstancedObjects;
	TArray<TArray<FTransform>> InstancedTransforms;
	TArray<int32> VariationOriginalObjectIndices;
	TArray<int32> VariationIndices;
	UpdateInstanceVariationObjects(
		OutputIdentifier,
		OriginalInstancedObjects, OriginalInstancedTransforms,
		InParentOutput->GetInstancedOutputs(),
		InstancedObjects, InstancedTransforms,
		VariationOriginalObjectIndices, VariationIndices);

	// Find the HGPO for this instanced output
	bool FoundHGPO = false;
	FHoudiniGeoPartObject HGPO;
	for (const auto& curHGPO : InParentOutput->GetHoudiniGeoPartObjects())
	{
		if (OutputIdentifier.Matches(curHGPO))
		{
			HGPO = curHGPO;
			FoundHGPO = true;
			break;
		}
	}

	if (!FoundHGPO)
	{
		// TODO check failure
		ensure(FoundHGPO);
	}

	// Extract the generic attributes for that HGPO
	TArray<FHoudiniGenericAttribute> AllPropertyAttributes;
	GetGenericPropertiesAttributes(OutputIdentifier.GeoId, OutputIdentifier.PartId, AllPropertyAttributes);

	// Check if this is a No-Instancers ( unreal_split_instances )
	bool bSplitMeshInstancer = IsSplitInstancer(OutputIdentifier.GeoId, OutputIdentifier.PartId);

	// See if we have instancer material overrides
	TArray<UMaterialInterface*> InstancerMaterials;
	if (!GetInstancerMaterials(OutputIdentifier.GeoId, OutputIdentifier.PartId, InstancerMaterials))
		InstancerMaterials.Empty();

	// Keep track of the new instancer component in order to be able to clean up the unused/stale ones after.
	TMap<FHoudiniOutputObjectIdentifier, FHoudiniOutputObject>& OutputObjects = InParentOutput->GetOutputObjects();
	TMap<FHoudiniOutputObjectIdentifier, FHoudiniOutputObject> ToDeleteOutputObjects = InParentOutput->GetOutputObjects();

	// Create the instancer components now
	for (int32 InstanceObjectIdx = 0; InstanceObjectIdx < InstancedObjects.Num(); InstanceObjectIdx++)
	{
		UObject* InstancedObject = InstancedObjects[InstanceObjectIdx].LoadSynchronous();
		if (!InstancedObject || InstancedObject->IsPendingKill())
			continue;

		if (!InstancedTransforms.IsValidIndex(InstanceObjectIdx))
			continue;

		const TArray<FTransform>& InstancedObjectTransforms = InstancedTransforms[InstanceObjectIdx];
		if (InstancedObjectTransforms.Num() <= 0)
			continue;

		// Update the split identifier for this object
		// We use both the original object index and the variation index: ORIG_VAR
		// the original object index is used for the instanced outputs split identifier
		OutputIdentifier.SplitIdentifier =
			InOutputIdentifier.SplitIdentifier
			+ TEXT("_")
			+ FString::FromInt(VariationIndices[InstanceObjectIdx]);

		// See if we can find an preexisting component for this obj	to try to reuse it
		USceneComponent* OldInstancerComponent = nullptr;
		FHoudiniOutputObject* FoundOutputObject = OutputObjects.Find(OutputIdentifier);
		if (FoundOutputObject)
		{
			OldInstancerComponent = Cast<USceneComponent>(FoundOutputObject->OutputComponent);
		}

		// Extract the material for this variation
//		FHoudiniInstancedOutput* FoundInstancedOutput = InstancedOutputs.Find(OutputIdentifier);
		TArray<UMaterialInterface*> VariationMaterials;
		if (!GetVariationMaterials(&InInstancedOutput, InstanceObjectIdx, InstancerMaterials, VariationMaterials))
			VariationMaterials.Empty();

		USceneComponent* NewInstancerComponent = nullptr;
		if (!CreateOrUpdateInstanceComponent(
			InstancedObject, InstancedObjectTransforms,
			AllPropertyAttributes, HGPO,
			InParentComponent, OldInstancerComponent, NewInstancerComponent,
			bSplitMeshInstancer, InstancerMaterials))
		{
			// TODO??
			continue;
		}

		if (!NewInstancerComponent)
			continue;

		if (OldInstancerComponent != NewInstancerComponent)
		{
			// Previous component wasn't reused, detach and delete it
			RemoveAndDestroyComponent(OldInstancerComponent);

			// Replace it with the new component
			if (FoundOutputObject)
			{
				FoundOutputObject->OutputComponent = NewInstancerComponent;
			}
			else
			{
				FHoudiniOutputObject& NewOutputObject = OutputObjects.Add(OutputIdentifier);
				NewOutputObject.OutputComponent = NewInstancerComponent;
			}
		}

		// Remove this output object from the todelete map
		ToDeleteOutputObjects.Remove(OutputIdentifier);
	}

	// Clean up the output objects that are not "reused" by the instanced outs
	// The ToDelete map now only contains unused/stale components, delete them
	for (auto& ToDeletePair : ToDeleteOutputObjects)
	{
		// Get the old Identifier / StaticMesh
		FHoudiniOutputObjectIdentifier& ToDeleteIdentifier = ToDeletePair.Key;
		UObject* OldComponent = ToDeletePair.Value.OutputComponent;
		if (OldComponent)
		{
			RemoveAndDestroyComponent(OldComponent);
			ToDeletePair.Value.OutputComponent = nullptr;
		}

		UObject* OldProxyComponent = ToDeletePair.Value.ProxyComponent;
		if (OldProxyComponent)
		{
			RemoveAndDestroyComponent(OldProxyComponent);
			ToDeletePair.Value.ProxyComponent = nullptr;
		}

		// Make sure the stale output object is not in the output map anymore
		OutputObjects.Remove(ToDeleteIdentifier);
	}
	ToDeleteOutputObjects.Empty();

	return true;
}


bool
FHoudiniInstanceTranslator::GetInstancerObjectsAndTransforms(
	const FHoudiniGeoPartObject& InHGPO,
	const TArray<UHoudiniOutput*>& InAllOutputs,
	TArray<UObject*>& OutInstancedObjects,
	TArray<TArray<FTransform>>& OutInstancedTransforms)
{
	TArray<UObject*> InstancedObjects;
	TArray<TArray<FTransform>> InstancedTransforms;

	TArray<FHoudiniGeoPartObject> InstancedHGPOs;
	TArray<TArray<FTransform>> InstancedHGPOTransforms;

	bool bSuccess = false;
	switch (InHGPO.InstancerType)
	{
		case EHoudiniInstancerType::PackedPrimitive:
		{
			bSuccess = GetPackedPrimitiveInstancerHGPOsAndTransforms(InHGPO, InstancedHGPOs, InstancedHGPOTransforms);
		}
		break;

		case EHoudiniInstancerType::AttributeInstancer:
		{
			// "Modern" attribute instancer - "unreal_instance"
			bSuccess = GetAttributeInstancerObjectsAndTransforms(InHGPO, InstancedObjects, InstancedTransforms);
		}
		break;

		case EHoudiniInstancerType::OldSchoolAttributeInstancer:
		{
			// Old school attribute override instancer - instance attribute w/ a HoudiniPath
			bSuccess = GetOldSchoolAttributeInstancerHGPOsAndTransforms(InHGPO, InAllOutputs, InstancedHGPOs, InstancedHGPOTransforms);
		}
		break;

		case EHoudiniInstancerType::ObjectInstancer:
		{
			// Old School object instancer
			bSuccess = GetObjectInstancerHGPOsAndTransforms(InHGPO, InAllOutputs, InstancedHGPOs, InstancedHGPOTransforms);
		}	
		break;
	}

	if (!bSuccess)
		return false;

	// Fetch the UOBject that correspond to the instanced parts
	// Attribute instancers don't need to do this since they refer UObjects directly
	if (InstancedHGPOs.Num() > 0)
	{
		for (int32 HGPOIdx = 0; HGPOIdx < InstancedHGPOs.Num(); HGPOIdx++)
		{
			const FHoudiniGeoPartObject& CurrentHGPO = InstancedHGPOs[HGPOIdx];

			// Get the UObject that was generated for that HGPO
			TArray<UObject*> ObjectsToInstance;
			for (const auto& Output : InAllOutputs)
			{
				if (!Output || Output->Type != EHoudiniOutputType::Mesh)
					continue;

				if (Output->OutputObjects.Num() <= 0)
					continue;

				for (const auto& OutObjPair : Output->OutputObjects)
				{					
					if (!OutObjPair.Key.Matches(CurrentHGPO))
						continue;

					const FHoudiniOutputObject& CurrentOutputObject = OutObjPair.Value;

					// In the case of a single-instance we can use the proxy (if it is current)
					// FHoudiniOutputTranslator::UpdateOutputs doesn't allow proxies if there is more than one instance in an output
					if (InstancedHGPOTransforms[HGPOIdx].Num() <= 1 && CurrentOutputObject.bProxyIsCurrent 
						&& CurrentOutputObject.ProxyObject && !CurrentOutputObject.ProxyObject->IsPendingKill())
					{
						ObjectsToInstance.Add(CurrentOutputObject.ProxyObject);
					}
					else if (CurrentOutputObject.OutputObject && !CurrentOutputObject.OutputObject->IsPendingKill())
					{
						ObjectsToInstance.Add(CurrentOutputObject.OutputObject);
					}
				}
			}

			// Add the UObject and the HGPO transforms to the output arrays
			for (const auto& MatchingOutputObj : ObjectsToInstance)
			{
				InstancedObjects.Add(MatchingOutputObj);
				InstancedTransforms.Add(InstancedHGPOTransforms[HGPOIdx]);
			}
		}
	}
	   
	//
	if (InstancedObjects.Num() <= 0 || InstancedTransforms.Num() != InstancedObjects.Num() )
	{
		// TODO
		// Error / warning
		return false;
	}

	OutInstancedObjects = InstancedObjects;
	OutInstancedTransforms = InstancedTransforms;

	return true;
}


void
FHoudiniInstanceTranslator::UpdateInstanceVariationObjects(
	const FHoudiniOutputObjectIdentifier& InOutputIdentifier,
	const TArray<UObject*>& InOriginalObjects,
	const TArray<TArray<FTransform>>& InOriginalTransforms,
	TMap<FHoudiniOutputObjectIdentifier, FHoudiniInstancedOutput>& InstancedOutputs,
	TArray<TSoftObjectPtr<UObject>>& OutVariationsInstancedObjects,
	TArray<TArray<FTransform>>& OutVariationsInstancedTransforms,
	TArray<int32>& OutVariationOriginalObjectIdx,
	TArray<int32>& OutVariationIndices)
{
	FHoudiniOutputObjectIdentifier Identifier = InOutputIdentifier;
	for (int32 InstObjIdx = 0; InstObjIdx < InOriginalObjects.Num(); InstObjIdx++)
	{
		UObject* OriginalObj = InOriginalObjects[InstObjIdx];
		if (!OriginalObj || OriginalObj->IsPendingKill())
			continue;

		// Build this output object's split identifier
		Identifier.SplitIdentifier = FString::FromInt(InstObjIdx);

		// Do we have an instanced output object for this one?
		FHoudiniInstancedOutput * FoundInstancedOutput = nullptr;
		for (auto& Iter : InstancedOutputs)
		{
			FHoudiniOutputObjectIdentifier& FoundIdentifier = Iter.Key;
			if (!(FoundIdentifier == Identifier))
				continue;

			// We found an existing instanced output for this identifier
			FoundInstancedOutput = &(Iter.Value);

			if (FoundIdentifier.bLoaded)
			{
				// The output object identifier we found is marked as loaded,
				// so uses old node IDs, we must update them, or the next cook
				// will fail to locate the output back
				FoundIdentifier.ObjectId = Identifier.ObjectId;
				FoundIdentifier.GeoId = Identifier.GeoId;
				FoundIdentifier.PartId = Identifier.PartId;
			}
		}

		if (!FoundInstancedOutput)
		{
			// Create a new one
			FHoudiniInstancedOutput CurInstancedOutput;
			CurInstancedOutput.OriginalObject = OriginalObj;
			CurInstancedOutput.OriginalObjectIndex = InstObjIdx;
			CurInstancedOutput.OriginalTransforms = InOriginalTransforms[InstObjIdx];

			CurInstancedOutput.VariationObjects.Add(OriginalObj);
			CurInstancedOutput.VariationTransformOffsets.Add(FTransform::Identity);
			CurInstancedOutput.TransformVariationIndices.SetNumZeroed(InOriginalTransforms[InstObjIdx].Num());
			CurInstancedOutput.MarkChanged(false);
			CurInstancedOutput.bStale = false;

			// No variations, simply assign the object/transforms
			OutVariationsInstancedObjects.Add(OriginalObj);
			OutVariationsInstancedTransforms.Add(InOriginalTransforms[InstObjIdx]);
			OutVariationOriginalObjectIdx.Add(InstObjIdx);
			OutVariationIndices.Add(0);

			InstancedOutputs.Add(Identifier, CurInstancedOutput);
		}
		else
		{
			// Process the potential variations
			FHoudiniInstancedOutput& CurInstancedOutput = *FoundInstancedOutput;
			UObject *ReplacedOriginalObject = nullptr;
			if (CurInstancedOutput.OriginalObject != OriginalObj)
			{
				ReplacedOriginalObject = CurInstancedOutput.OriginalObject.LoadSynchronous();
				CurInstancedOutput.OriginalObject = OriginalObj;
			}

			CurInstancedOutput.OriginalTransforms = InOriginalTransforms[InstObjIdx];

			// Shouldnt be needed...
			CurInstancedOutput.OriginalObjectIndex = InstObjIdx;

			// Remove any null or deleted variation objects
			TArray<int32> ObjsToRemove;
			for (int32 VarIdx = CurInstancedOutput.VariationObjects.Num() - 1; VarIdx >= 0; --VarIdx)
			{
				UObject* CurrentVariationObject = CurInstancedOutput.VariationObjects[VarIdx].LoadSynchronous();
				if (!CurrentVariationObject || CurrentVariationObject->IsPendingKill() || (ReplacedOriginalObject && ReplacedOriginalObject == CurrentVariationObject))
				{
					ObjsToRemove.Add(VarIdx);
				}
			}
			if (ObjsToRemove.Num() > 0)
			{
				for (const int32 &VarIdx : ObjsToRemove)
				{
					CurInstancedOutput.VariationObjects.RemoveAt(VarIdx);
					CurInstancedOutput.VariationTransformOffsets.RemoveAt(VarIdx);
				}
				// Force a recompute of variation assignments
				CurInstancedOutput.TransformVariationIndices.SetNum(0);
			}

			// If we don't have variations, simply use the original object
			if (CurInstancedOutput.VariationObjects.Num() <= 0)
			{
				// No variations? add the original one
				CurInstancedOutput.VariationObjects.Add(OriginalObj);
				CurInstancedOutput.VariationTransformOffsets.Add(FTransform::Identity);
				CurInstancedOutput.TransformVariationIndices.SetNum(0);
			}

			// If the number of transforms has changed since the previous cook, 
			// we need to recompute the variation assignments
			if (CurInstancedOutput.TransformVariationIndices.Num() != CurInstancedOutput.OriginalTransforms.Num())
				UpdateVariationAssignements(CurInstancedOutput);

			// Assign variations and their transforms
			for (int32 VarIdx = 0; VarIdx < CurInstancedOutput.VariationObjects.Num(); VarIdx++)
			{
				UObject* CurrentVariationObject = CurInstancedOutput.VariationObjects[VarIdx].LoadSynchronous();
				if (!CurrentVariationObject || CurrentVariationObject->IsPendingKill())
					continue;

				// Get the transforms assigned to that variation
				TArray<FTransform> ProcessedTransforms;
				ProcessInstanceTransforms(CurInstancedOutput, VarIdx, ProcessedTransforms);
				if (ProcessedTransforms.Num() > 0)
				{
					OutVariationsInstancedObjects.Add(CurrentVariationObject);
					OutVariationsInstancedTransforms.Add(ProcessedTransforms);
					OutVariationOriginalObjectIdx.Add(InstObjIdx);
					OutVariationIndices.Add(VarIdx);
				}
			}

			CurInstancedOutput.MarkChanged(false);
			CurInstancedOutput.bStale = false;
		}
	}
}


void
FHoudiniInstanceTranslator::UpdateVariationAssignements(FHoudiniInstancedOutput& InstancedOutput)
{
	int32 TransformCount = InstancedOutput.OriginalTransforms.Num();
	InstancedOutput.TransformVariationIndices.SetNumZeroed(TransformCount);

	int32 VariationCount = InstancedOutput.VariationObjects.Num();
	if (VariationCount <= 1)
		return;

	int nSeed = 1234;	
	for (int32 Idx = 0; Idx < TransformCount; Idx++)
	{
		InstancedOutput.TransformVariationIndices[Idx] = fastrand(nSeed) % VariationCount;
	}	
}

void
FHoudiniInstanceTranslator::ProcessInstanceTransforms(
	FHoudiniInstancedOutput& InstancedOutput, const int32& VariationIdx, TArray<FTransform>& OutProcessedTransforms)
{
	if (!InstancedOutput.VariationObjects.IsValidIndex(VariationIdx))
		return;

	if (!InstancedOutput.VariationTransformOffsets.IsValidIndex(VariationIdx))
		return;

	bool bHasVariations = InstancedOutput.VariationObjects.Num() > 1;
	bool bHasTransformOffset = InstancedOutput.VariationTransformOffsets.IsValidIndex(VariationIdx)
		? !InstancedOutput.VariationTransformOffsets[VariationIdx].Equals(FTransform::Identity)
		: false;

	if (!bHasVariations && !bHasTransformOffset)
	{
		// We dont have variations or transform offset, so we can reuse the original transforms as is
		OutProcessedTransforms = InstancedOutput.OriginalTransforms;
		return;
	}

	if (bHasVariations)
	{
		// We simply need to extract the transforms for this variation		
		for (int32 TransformIndex = 0; TransformIndex < InstancedOutput.TransformVariationIndices.Num(); TransformIndex++)
		{
			if (InstancedOutput.TransformVariationIndices[TransformIndex] != VariationIdx)
				continue;

			OutProcessedTransforms.Add(InstancedOutput.OriginalTransforms[TransformIndex]);
		}
	}
	else
	{
		// No variations, we can reuse the original transforms
		OutProcessedTransforms = InstancedOutput.OriginalTransforms;
	}

	if (bHasTransformOffset)
	{
		// Get the transform offset for this variation
		FVector PositionOffset = InstancedOutput.VariationTransformOffsets[VariationIdx].GetLocation();
		FQuat RotationOffset = InstancedOutput.VariationTransformOffsets[VariationIdx].GetRotation();
		FVector ScaleOffset = InstancedOutput.VariationTransformOffsets[VariationIdx].GetScale3D();

		FTransform CurrentTransform = FTransform::Identity;
		for (int32 TransformIndex = 0; TransformIndex < OutProcessedTransforms.Num(); TransformIndex++)
		{
			CurrentTransform = OutProcessedTransforms[TransformIndex];

			// Compute new rotation and scale.
			FVector Position = CurrentTransform.GetLocation() + PositionOffset;
			FQuat TransformRotation = CurrentTransform.GetRotation() * RotationOffset;
			FVector TransformScale3D = CurrentTransform.GetScale3D() * ScaleOffset;

			// Make sure inverse matrix exists - seems to be a bug in Unreal when submitting instances.
			// Happens in blueprint as well.
			// We want to make sure the scale is not too small, but keep negative values! (Bug 90876)
			if (FMath::Abs(TransformScale3D.X) < HAPI_UNREAL_SCALE_SMALL_VALUE)
				TransformScale3D.X = (TransformScale3D.X > 0) ? HAPI_UNREAL_SCALE_SMALL_VALUE : -HAPI_UNREAL_SCALE_SMALL_VALUE;

			if (FMath::Abs(TransformScale3D.Y) < HAPI_UNREAL_SCALE_SMALL_VALUE)
				TransformScale3D.Y = (TransformScale3D.Y > 0) ? HAPI_UNREAL_SCALE_SMALL_VALUE : -HAPI_UNREAL_SCALE_SMALL_VALUE;

			if (FMath::Abs(TransformScale3D.Z) < HAPI_UNREAL_SCALE_SMALL_VALUE)
				TransformScale3D.Z = (TransformScale3D.Z > 0) ? HAPI_UNREAL_SCALE_SMALL_VALUE : -HAPI_UNREAL_SCALE_SMALL_VALUE;

			CurrentTransform.SetLocation(Position);
			CurrentTransform.SetRotation(TransformRotation);
			CurrentTransform.SetScale3D(TransformScale3D);

			if (CurrentTransform.IsValid())
				OutProcessedTransforms[TransformIndex] = CurrentTransform;
		}
	}
}

bool
FHoudiniInstanceTranslator::GetPackedPrimitiveInstancerHGPOsAndTransforms(
	const FHoudiniGeoPartObject& InHGPO,
	TArray<FHoudiniGeoPartObject>& OutInstancedHGPO,
	TArray<TArray<FTransform>>& OutInstancedTransforms)
{
	if (InHGPO.InstancerType != EHoudiniInstancerType::PackedPrimitive)
		return false;

	/*
	// This is using packed primitives
	HAPI_PartInfo PartInfo;
	FHoudiniApi::PartInfo_Init(&PartInfo);
	HOUDINI_CHECK_ERROR_RETURN( FHoudiniApi::GetPartInfo(
		FHoudiniEngine::Get().GetSession(), InHGPO.GeoId, InHGPO.PartId, &PartInfo), false);
	*/

	/*
	// Retrieve part name.
	FString PartName;
	FHoudiniEngineString::ToFString(PartInfo.nameSH, PartName);
	*/	

	// Get transforms for each instance
	TArray<HAPI_Transform> InstancerPartTransforms;
	InstancerPartTransforms.SetNumZeroed(InHGPO.PartInfo.InstanceCount);
	HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::GetInstancerPartTransforms(
		FHoudiniEngine::Get().GetSession(), InHGPO.GeoId, InHGPO.PartInfo.PartId,
		HAPI_RSTORDER_DEFAULT, InstancerPartTransforms.GetData(), 0, InHGPO.PartInfo.InstanceCount), false);

	// Convert the transform to Unreal's coordinate system
	TArray<FTransform> InstancerUnrealTransforms;
	InstancerUnrealTransforms.SetNumUninitialized(InstancerPartTransforms.Num());
	for (int32 InstanceIdx = 0; InstanceIdx < InstancerPartTransforms.Num(); InstanceIdx++)
	{
		const auto& InstanceTransform = InstancerPartTransforms[InstanceIdx];
		FHoudiniEngineUtils::TranslateHapiTransform(InstanceTransform, InstancerUnrealTransforms[InstanceIdx]);
	}

	// Get the part ids for parts being instanced
	TArray<HAPI_PartId> InstancedPartIds;
	InstancedPartIds.SetNumZeroed(InHGPO.PartInfo.InstancedPartCount);
	HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::GetInstancedPartIds(
		FHoudiniEngine::Get().GetSession(), InHGPO.GeoId, InHGPO.PartInfo.PartId,
		InstancedPartIds.GetData(), 0, InHGPO.PartInfo.InstancedPartCount), false);

	for (const auto& InstancedPartId : InstancedPartIds)
	{
		// Create a GeoPartObject corresponding to the instanced part
		FHoudiniGeoPartObject InstancedHGPO;
		InstancedHGPO.AssetId = InHGPO.AssetId;
		InstancedHGPO.AssetName = InHGPO.AssetName;
		InstancedHGPO.ObjectId = InHGPO.ObjectId;
		InstancedHGPO.ObjectName = InHGPO.ObjectName;
		InstancedHGPO.GeoId = InHGPO.GeoId;
		InstancedHGPO.PartId = InstancedPartId;
		InstancedHGPO.PartName = InHGPO.PartName;
		InstancedHGPO.TransformMatrix = InHGPO.TransformMatrix;		

		// TODO: Copy more cached data?

		OutInstancedHGPO.Add(InstancedHGPO);
		OutInstancedTransforms.Add(InstancerUnrealTransforms);
	}

	return true;
}


bool
FHoudiniInstanceTranslator::GetAttributeInstancerObjectsAndTransforms(
	const FHoudiniGeoPartObject& InHGPO,
	TArray<UObject*>& OutInstancedObjects,
	TArray<TArray<FTransform>>& OutInstancedTransforms)
{
	if (InHGPO.InstancerType != EHoudiniInstancerType::AttributeInstancer)
		return false;

	// Look for the unreal instance attribute
	HAPI_AttributeInfo AttribInfo;
	FHoudiniApi::AttributeInfo_Init(&AttribInfo);

	// instance attribute on points
	bool is_override_attr = false;
	HAPI_Result Result = FHoudiniApi::GetAttributeInfo(
		FHoudiniEngine::Get().GetSession(),
		InHGPO.GeoId, InHGPO.PartId,
		HAPI_UNREAL_ATTRIB_INSTANCE, HAPI_ATTROWNER_POINT, &AttribInfo);
	
	// unreal_instance attribute on points
	if (Result != HAPI_RESULT_SUCCESS || AttribInfo.exists == false)
	{
		is_override_attr = true;
		Result = FHoudiniApi::GetAttributeInfo(
			FHoudiniEngine::Get().GetSession(),
			InHGPO.GeoId, InHGPO.PartId,
			HAPI_UNREAL_ATTRIB_INSTANCE_OVERRIDE, HAPI_ATTROWNER_POINT, &AttribInfo);
	}

	// unreal_instance attribute on detail
	if (Result != HAPI_RESULT_SUCCESS || !AttribInfo.exists)
	{
		is_override_attr = true;
		Result = FHoudiniApi::GetAttributeInfo(
			FHoudiniEngine::Get().GetSession(),
			InHGPO.GeoId, InHGPO.PartId,
			HAPI_UNREAL_ATTRIB_INSTANCE_OVERRIDE, HAPI_ATTROWNER_DETAIL, &AttribInfo);
	}

	// Attribute does not exist.
	if (Result != HAPI_RESULT_SUCCESS || !AttribInfo.exists)
		return false;

	// Get the instance transforms
	TArray<FTransform> InstancerUnrealTransforms;
	if (!HapiGetInstanceTransforms(InHGPO, InstancerUnrealTransforms))
	{
		// failed to get instance transform
		return false;
	}
	
	if (AttribInfo.owner == HAPI_ATTROWNER_DETAIL)
	{
		// If the attribute is on the detail, then its value is applied to all points
		TArray<FString> DetailInstanceValues;
		if ( !FHoudiniEngineUtils::HapiGetAttributeDataAsStringFromInfo(
			InHGPO.GeoId, InHGPO.PartId, AttribInfo,
			is_override_attr ? HAPI_UNREAL_ATTRIB_INSTANCE_OVERRIDE : HAPI_UNREAL_ATTRIB_INSTANCE,
			DetailInstanceValues) )
		{
			// This should not happen - attribute exists, but there was an error retrieving it.
			return false;
		}

		if (DetailInstanceValues.Num() <= 0)
		{
			// No values specified.
			return false;
		}

		// Attempt to load specified asset.
		const FString & AssetName = DetailInstanceValues[0];
		UObject * AttributeObject = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetName, nullptr, LOAD_None, nullptr);
		if (!AttributeObject)
		{
			// Couldnt load the referenced object
			return false;
		}			

		OutInstancedObjects.Add(AttributeObject);
		OutInstancedTransforms.Add(InstancerUnrealTransforms);
	}
	else
	{
		// Attribute is on points, so we may have different values for each of them
		TArray<FString> PointInstanceValues;
		if (!FHoudiniEngineUtils::HapiGetAttributeDataAsStringFromInfo(
			InHGPO.GeoId, InHGPO.PartId, AttribInfo,
			is_override_attr ? HAPI_UNREAL_ATTRIB_INSTANCE_OVERRIDE : HAPI_UNREAL_ATTRIB_INSTANCE,
			PointInstanceValues))
		{
			// This should not happen - attribute exists, but there was an error retrieving it.
			return false;
		}

		// The attribute is on points, so the number of points must match number of transforms.
		if (!ensure(PointInstanceValues.Num() == InstancerUnrealTransforms.Num()))
		{
			// This should not happen, we have mismatch between number of instance values and transforms.
			return false;
		}

		// If instance attribute exists on points, we need to get all the unique values.
		// This will give us all the unique object we want to instance
		TMap<FString, UObject *> ObjectsToInstance;
		for (const auto& Iter : PointInstanceValues)
		{
			if (!ObjectsToInstance.Contains(Iter))
			{
				// To avoid trying to load an object that fails multiple times,
				// still add it to the array if null so we can still skip further attempts
				UObject * AttributeObject = StaticLoadObject(
					UObject::StaticClass(), nullptr, *Iter, nullptr, LOAD_None, nullptr);

				ObjectsToInstance.Add(Iter, AttributeObject);
			}
		}

		// Iterates through all the unique objects and get their corresponding transforms
		bool Success = false;
		for (auto Iter : ObjectsToInstance)
		{
			// Check that we managed to load this object
			UObject * AttributeObject = Iter.Value;
			if (!AttributeObject)
				continue;

			// Extract the transform values that correspond to this object
			const FString & InstancePath = Iter.Key;
			TArray<FTransform> ObjectTransforms;
			for (int32 Idx = 0; Idx < PointInstanceValues.Num(); ++Idx)
			{
				if (InstancePath.Equals(PointInstanceValues[Idx]))
					ObjectTransforms.Add(InstancerUnrealTransforms[Idx]);
			}

			OutInstancedObjects.Add(AttributeObject);
			OutInstancedTransforms.Add(ObjectTransforms);
			Success = true;
		}

		if (!Success)
			return false;
	}

	return true;
}


bool
FHoudiniInstanceTranslator::GetOldSchoolAttributeInstancerHGPOsAndTransforms(
	const FHoudiniGeoPartObject& InHGPO,
	const TArray<UHoudiniOutput*>& InAllOutputs,
	TArray<FHoudiniGeoPartObject>& OutInstancedHGPO,
	TArray<TArray<FTransform>>& OutInstancedTransforms)
{
	if (InHGPO.InstancerType != EHoudiniInstancerType::OldSchoolAttributeInstancer)
		return false;

	// Get the instance transforms
	TArray<FTransform> InstancerUnrealTransforms;
	if (!HapiGetInstanceTransforms(InHGPO, InstancerUnrealTransforms))
	{
		// failed to get instance transform
		return false;
	}

	// Get the objects IDs to instanciate
	int32 NumPoints = InHGPO.PartInfo.PointCount;
	TArray<HAPI_NodeId> InstancedObjectIds;
	InstancedObjectIds.SetNumUninitialized(NumPoints);
	HOUDINI_CHECK_ERROR_RETURN( FHoudiniApi::GetInstancedObjectIds(
		FHoudiniEngine::Get().GetSession(), 
		InHGPO.GeoId, InstancedObjectIds.GetData(), 0, NumPoints), false);

	// Find the set of instanced object ids and locate the corresponding parts
	TSet<int32> UniqueInstancedObjectIds(InstancedObjectIds);
	
	// Locate all the HoudiniGeoPartObject that corresponds to the instanced object IDs
	for (int32 InstancedObjectId : UniqueInstancedObjectIds)
	{
		// Get the parts that correspond to that object Id
		TArray<FHoudiniGeoPartObject> PartsToInstance;
		for (const auto& Output : InAllOutputs)
		{
			if (!Output || Output->Type != EHoudiniOutputType::Mesh)
				continue;
			
			for (const auto& OutHGPO : Output->HoudiniGeoPartObjects)
			{
				if (OutHGPO.Type != EHoudiniPartType::Mesh)
					continue;

				if (OutHGPO.bIsInstanced)
					continue;

				if (InstancedObjectId != OutHGPO.ObjectId)
					continue;

				PartsToInstance.Add(OutHGPO);
			}
		}

		// Extract only the transforms that correspond to that specific object ID
		TArray<FTransform> InstanceTransforms;
		for (int32 Ix = 0; Ix < InstancedObjectIds.Num(); ++Ix)
		{
			if ((InstancedObjectIds[Ix] == InstancedObjectId) && (InstancerUnrealTransforms.IsValidIndex(Ix)))
			{
				InstanceTransforms.Add(InstancerUnrealTransforms[Ix]);
			}
		}

		// Add the instanced parts and their transforms to the output arrays
		for (const auto& PartToInstance : PartsToInstance)
		{
			OutInstancedHGPO.Add(PartToInstance);
			OutInstancedTransforms.Add(InstanceTransforms);
		}
	}

	if(OutInstancedHGPO.Num() > 0 && OutInstancedTransforms.Num() > 0)
		return true;

	return false;
}


bool
FHoudiniInstanceTranslator::GetObjectInstancerHGPOsAndTransforms(
	const FHoudiniGeoPartObject& InHGPO,
	const TArray<UHoudiniOutput*>& InAllOutputs,
	TArray<FHoudiniGeoPartObject>& OutInstancedHGPO,
	TArray<TArray<FTransform>>& OutInstancedTransforms)
{
	if (InHGPO.InstancerType != EHoudiniInstancerType::ObjectInstancer)
		return false;

	if (InHGPO.ObjectInfo.ObjectToInstanceID < 0)
		return false;

	// Get the instance transforms
	TArray<FTransform> InstancerUnrealTransforms;
	if (!HapiGetInstanceTransforms(InHGPO, InstancerUnrealTransforms))
	{
		// failed to get instance transform
		return false;
	}

	// Get the parts that correspond to that Object Id
	TArray<FHoudiniGeoPartObject> PartsToInstance;
	for (const auto& Output : InAllOutputs)
	{
		if (!Output || Output->Type != EHoudiniOutputType::Mesh)
			continue;

		for (const auto& OutHGPO : Output->HoudiniGeoPartObjects)
		{
			if (OutHGPO.Type != EHoudiniPartType::Mesh)
				continue;

			/*
			// But the instanced geo is actually not marked as instanced
			if (!OutHGPO.bIsInstanced)
				continue;
			*/

			if (InHGPO.ObjectInfo.ObjectToInstanceID != OutHGPO.ObjectId)
				continue;

			PartsToInstance.Add(OutHGPO);
		}
	}

	// Add found HGPO and transforms to the output arrays
	for (auto& InstanceHGPO : PartsToInstance)
	{
		InstanceHGPO.TransformMatrix = InHGPO.TransformMatrix;

		// TODO:
		//InstanceHGPO.UpdateCustomName();

		OutInstancedHGPO.Add(InstanceHGPO);
		OutInstancedTransforms.Add(InstancerUnrealTransforms);
	}

	return true;
}

bool
FHoudiniInstanceTranslator::CreateOrUpdateInstanceComponent(
	UObject* InstancedObject,
	const TArray<FTransform>& InstancedObjectTransforms,
	const TArray<FHoudiniGenericAttribute>& AllPropertyAttributes,
	const FHoudiniGeoPartObject& InstancerGeoPartObject,
	USceneComponent* ParentComponent,
	USceneComponent* OldComponent,
	USceneComponent*& NewComponent,	
	const bool& InIsSplitMeshInstancer,
	const TArray<UMaterialInterface *>& InstancerMaterials,
	const int32& InstancerObjectIdx)
{
	enum InstancerComponentType
	{
		Invalid = -1,
		InstancedStaticMeshComponent = 0,
		HierarchicalInstancedStaticMeshComponent = 1,
		MeshSplitInstancerComponent = 2,
		HoudiniInstancedActorComponent = 3,
		StaticMeshComponent = 4,
		HoudiniStaticMeshComponent = 5
	};

	// See if we can reuse the old component
	InstancerComponentType OldType = InstancerComponentType::Invalid;
	if (OldComponent && !OldComponent->IsPendingKill())
	{
		if (OldComponent->IsA<UHierarchicalInstancedStaticMeshComponent>())
			OldType = HierarchicalInstancedStaticMeshComponent;
		else if (OldComponent->IsA<UInstancedStaticMeshComponent>())
			OldType = InstancedStaticMeshComponent;
		else if (OldComponent->IsA<UHoudiniMeshSplitInstancerComponent>())
			OldType = MeshSplitInstancerComponent;
		else if (OldComponent->IsA<UHoudiniInstancedActorComponent>())
			OldType = HoudiniInstancedActorComponent;
		else if (OldComponent->IsA<UStaticMeshComponent>())
			OldType = StaticMeshComponent;
		else if (OldComponent->IsA<UHoudiniStaticMeshComponent>())
			OldType = HoudiniStaticMeshComponent;
	}	

	// See what type of component we want to create
	InstancerComponentType NewType = InstancerComponentType::Invalid;
	UStaticMesh * StaticMesh = Cast<UStaticMesh>(InstancedObject);
	UHoudiniStaticMesh * HSM = nullptr;
	if (!StaticMesh)
		HSM = Cast<UHoudiniStaticMesh>(InstancedObject);
	if (StaticMesh)
	{
		if (InstancedObjectTransforms.Num() == 1)
			NewType = StaticMeshComponent;
		else if (InIsSplitMeshInstancer)
			NewType = MeshSplitInstancerComponent;
		else if(StaticMesh->GetNumLODs() > 1)
			NewType = HierarchicalInstancedStaticMeshComponent;
		else
			NewType = InstancedStaticMeshComponent;
	}
	else if (HSM)
	{
		if (InstancedObjectTransforms.Num() == 1)
			NewType = HoudiniStaticMeshComponent;
		else
		{
			HOUDINI_LOG_ERROR(TEXT("More than one instance transform encountered for UHoudiniStaticMesh: %s"), *(HSM->GetPathName()));
			NewType = Invalid;
			return false;
		}
	}
	else
	{
		NewType = HoudiniInstancedActorComponent;
	}

	if (OldType == NewType)
		NewComponent = OldComponent;

	UMaterialInterface* InstancerMaterial = nullptr;
	if (InstancerMaterials.Num() > 0)
	{
		if (InstancerMaterials.IsValidIndex(InstancerObjectIdx))
			InstancerMaterial = InstancerMaterials[InstancerObjectIdx];
		else
			InstancerMaterial = InstancerMaterials[0];
	}

	bool bSuccess = false;
	switch (NewType)
	{
		case InstancedStaticMeshComponent:
		case HierarchicalInstancedStaticMeshComponent:
		{
			// Create an Instanced Static Mesh Component
			bSuccess = CreateOrUpdateInstancedStaticMeshComponent(
				StaticMesh, InstancedObjectTransforms, AllPropertyAttributes, InstancerGeoPartObject, ParentComponent, NewComponent, InstancerMaterial);
		}
		break;

		case MeshSplitInstancerComponent:
		{
			bSuccess = CreateOrUpdateMeshSplitInstancerComponent(
				StaticMesh, InstancedObjectTransforms, AllPropertyAttributes, InstancerGeoPartObject, ParentComponent, NewComponent, InstancerMaterials);
		}
		break;

		case HoudiniInstancedActorComponent:
		{
			bSuccess = CreateOrUpdateInstancedActorComponent(
				InstancedObject, InstancedObjectTransforms, AllPropertyAttributes, ParentComponent, NewComponent);
		}
		break;

		case StaticMeshComponent:
		{
			// Create a Static Mesh Component
			bSuccess = CreateOrUpdateStaticMeshComponent(
				StaticMesh, InstancedObjectTransforms, AllPropertyAttributes, InstancerGeoPartObject, ParentComponent, NewComponent, InstancerMaterial);
		}
		break;

		case HoudiniStaticMeshComponent:
		{
			// Create a Houdini Static Mesh Component
			bSuccess = CreateOrUpdateHoudiniStaticMeshComponent(
				HSM, InstancedObjectTransforms, AllPropertyAttributes, InstancerGeoPartObject, ParentComponent, NewComponent, InstancerMaterial);
		}
		break;
	}

	if (!NewComponent)
		return false;

	NewComponent->SetMobility(ParentComponent->Mobility);
	NewComponent->AttachToComponent(ParentComponent, FAttachmentTransformRules::KeepRelativeTransform);

	// For single instance, that generates a SMC, the transform is already set on the component
	// TODO: Should cumulate transform in that case?
	if(NewType != StaticMeshComponent && NewType != HoudiniStaticMeshComponent)
		NewComponent->SetRelativeTransform(InstancerGeoPartObject.TransformMatrix);

	// Only register if we have a valid component
	if (NewComponent->GetOwner() && NewComponent->GetWorld())
		NewComponent->RegisterComponent();

	// If the old component couldn't be reused, dettach/ destroy it
	if (OldComponent && !OldComponent->IsPendingKill() && (OldComponent != NewComponent))
	{
		RemoveAndDestroyComponent(OldComponent);
	}

	return bSuccess;
}

bool
FHoudiniInstanceTranslator::CreateOrUpdateInstancedStaticMeshComponent(
	UStaticMesh* InstancedStaticMesh,
	const TArray<FTransform>& InstancedObjectTransforms,
	const TArray<FHoudiniGenericAttribute>& AllPropertyAttributes,
	const FHoudiniGeoPartObject& InstancerGeoPartObject,
	USceneComponent* ParentComponent,
	USceneComponent*& CreatedInstancedComponent,
	UMaterialInterface * InstancerMaterial /*=nullptr*/)
{
	if (!InstancedStaticMesh)
		return false;

	if (!ParentComponent || ParentComponent->IsPendingKill())
		return false;

	UObject* ComponentOuter = ParentComponent;
	if (ParentComponent->GetOwner() && !ParentComponent->GetOwner()->IsPendingKill())
		ComponentOuter = ParentComponent->GetOwner();

	bool bCreatedNewComponent = false;
	UInstancedStaticMeshComponent* InstancedStaticMeshComponent = Cast<UInstancedStaticMeshComponent>(CreatedInstancedComponent);
	if (!InstancedStaticMeshComponent || InstancedStaticMeshComponent->IsPendingKill())
	{
		if (InstancedStaticMesh->GetNumLODs() > 1)
		{
			// If the mesh has LODs, use Hierarchical ISMC
			InstancedStaticMeshComponent = NewObject<UHierarchicalInstancedStaticMeshComponent>(
				ComponentOuter, UHierarchicalInstancedStaticMeshComponent::StaticClass(), NAME_None, RF_Transactional);
		}
		else
		{
			// If the mesh doesnt have LOD, we can use a regular ISMC
			InstancedStaticMeshComponent = NewObject<UInstancedStaticMeshComponent>(
				ComponentOuter, UInstancedStaticMeshComponent::StaticClass(), NAME_None, RF_Transactional);
		}

		// Change the creation method so the component is listed in the details panels
		InstancedStaticMeshComponent->CreationMethod = EComponentCreationMethod::Instance;

		bCreatedNewComponent = true;
	}

	if (!InstancedStaticMeshComponent)
		return false;

	InstancedStaticMeshComponent->SetStaticMesh(InstancedStaticMesh);
	InstancedStaticMeshComponent->GetBodyInstance()->bAutoWeld = false;

	InstancedStaticMeshComponent->OverrideMaterials.Empty();
	if (InstancerMaterial)
	{
		int32 MeshMaterialCount = InstancedStaticMesh->StaticMaterials.Num();
		for (int32 Idx = 0; Idx < MeshMaterialCount; ++Idx)
			InstancedStaticMeshComponent->SetMaterial(Idx, InstancerMaterial);
	}

	// Now add the instances themselves
	// TODO: We should be calling  UHoudiniInstancedActorComponent::UpdateInstancerComponentInstances( ... )
	InstancedStaticMeshComponent->ClearInstances();
	InstancedStaticMeshComponent->PreAllocateInstancesMemory(InstancedObjectTransforms.Num());
	for (const auto& Transform : InstancedObjectTransforms)
	{
		InstancedStaticMeshComponent->AddInstance(Transform);
	}

	// Apply generic attributes if we have any
	// TODO: Handle variations w/ index
	for (const auto& CurrentAttrib : AllPropertyAttributes)
	{
		UpdateGenericPropertiesAttributes(InstancedStaticMeshComponent, AllPropertyAttributes, 0);
	}

	// Assign the new ISMC / HISMC to the output component if we created a new one
	if(bCreatedNewComponent)
		CreatedInstancedComponent = InstancedStaticMeshComponent;

	// TODO:
	// We want to make this invisible if it's a collision instancer.
	//CreatedInstancedComponent->SetVisibility(!InstancerGeoPartObject.bIsCollidable);

	return true;
}

bool
FHoudiniInstanceTranslator::CreateOrUpdateInstancedActorComponent(
	UObject* InstancedObject,
	const TArray<FTransform>& InstancedObjectTransforms,
	const TArray<FHoudiniGenericAttribute>& AllPropertyAttributes,
	USceneComponent* ParentComponent,
	USceneComponent*& CreatedInstancedComponent)
{
	if (!InstancedObject)
		return false;

	if (!ParentComponent || ParentComponent->IsPendingKill())
		return false;

	UObject* ComponentOuter = ParentComponent;
	if (ParentComponent->GetOwner() && !ParentComponent->GetOwner()->IsPendingKill())
		ComponentOuter = ParentComponent->GetOwner();

	bool bCreatedNewComponent = false;
	UHoudiniInstancedActorComponent* InstancedActorComponent = Cast<UHoudiniInstancedActorComponent>(CreatedInstancedComponent);
	if (!InstancedActorComponent || InstancedActorComponent->IsPendingKill())
	{
		// If the mesh doesnt have LOD, we can use a regular ISMC
		InstancedActorComponent = NewObject<UHoudiniInstancedActorComponent>(
			ComponentOuter, UHoudiniInstancedActorComponent::StaticClass(), NAME_None, RF_Transactional);
		
		// Change the creation method so the component is listed in the details panels
		InstancedActorComponent->CreationMethod = EComponentCreationMethod::Instance;

		bCreatedNewComponent = true;
	}

	if (!InstancedActorComponent)
		return false;

	// See if the instanced object has changed
	bool bInstancedObjectHasChanged = (InstancedObject != InstancedActorComponent->GetInstancedObject());
	if (bInstancedObjectHasChanged)
	{
		// All actors will need to be respawned, invalidate all of them
		InstancedActorComponent->ClearAllInstances();

		// Update the HIAC's instanced asset
		InstancedActorComponent->SetInstancedObject(InstancedObject);
	}

	// Get the level where we want to spawn the actors
	ULevel* SpawnLevel = ParentComponent->GetOwner() ? ParentComponent->GetOwner()->GetLevel() : nullptr;
	if (!SpawnLevel)
		return false;

	// Set the number of needed instances
	InstancedActorComponent->SetNumberOfInstances(InstancedObjectTransforms.Num());
	for (int32 Idx = 0; Idx < InstancedObjectTransforms.Num(); Idx++)
	{
		// if we already have an actor, we can reuse it
		const FTransform& CurTransform = InstancedObjectTransforms[Idx];

		// Get the current instance
		// If null, we need to create a new one, else we can reuse the actor
		AActor* CurInstance = InstancedActorComponent->GetInstancedActorAt(Idx);
		if (!CurInstance || CurInstance->IsPendingKill())
		{
#if WITH_EDITOR
			// Try to spawn a new actor for the given transform
			GEditor->ClickLocation = CurTransform.GetTranslation();
			GEditor->ClickPlane = FPlane(GEditor->ClickLocation, FVector::UpVector);

			TArray<AActor*> NewActors = FLevelEditorViewportClient::TryPlacingActorFromObject(SpawnLevel, InstancedObject, false, RF_Transactional, nullptr);
			if (NewActors.Num() > 0)
			{
				if (NewActors[0] && !NewActors[0]->IsPendingKill())
				{
					CurInstance = NewActors[0];
				}
			}

			InstancedActorComponent->SetInstanceAt(Idx, CurTransform, CurInstance);
#endif
		}
		else
		{
			// We can simply update the actor's transform
			InstancedActorComponent->SetInstanceTransformAt(Idx, CurTransform);
		}

		// Update the generic properties for that instance if any
		// TODO: Handle instance variations w/ Idx
		UpdateGenericPropertiesAttributes(CurInstance, AllPropertyAttributes, Idx);
	}

	// Assign the new ISMC / HISMC to the output component if we created a new one
	if (bCreatedNewComponent)
	{
		CreatedInstancedComponent = InstancedActorComponent;
	}

	return true;
}

// Create or update a MSIC
bool 
FHoudiniInstanceTranslator::CreateOrUpdateMeshSplitInstancerComponent(
	UStaticMesh* InstancedStaticMesh,
	const TArray<FTransform>& InstancedObjectTransforms,
	const TArray<FHoudiniGenericAttribute>& AllPropertyAttributes,
	const FHoudiniGeoPartObject& InstancerGeoPartObject,
	USceneComponent* ParentComponent,
	USceneComponent*& CreatedInstancedComponent,
	const TArray<UMaterialInterface *>& InInstancerMaterials)
{
	if (!InstancedStaticMesh)
		return false;

	if (!ParentComponent || ParentComponent->IsPendingKill())
		return false;

	UObject* ComponentOuter = ParentComponent;
	if (ParentComponent->GetOwner() && !ParentComponent->GetOwner()->IsPendingKill())
		ComponentOuter = ParentComponent->GetOwner();

	bool bCreatedNewComponent = false;
	UHoudiniMeshSplitInstancerComponent* MeshSplitComponent = Cast<UHoudiniMeshSplitInstancerComponent>(CreatedInstancedComponent);
	if (!MeshSplitComponent || MeshSplitComponent->IsPendingKill())
	{
		// If the mesh doesn't have LOD, we can use a regular ISMC
		MeshSplitComponent = NewObject<UHoudiniMeshSplitInstancerComponent>(
			ComponentOuter, UHoudiniMeshSplitInstancerComponent::StaticClass(), NAME_None, RF_Transactional);

		// Change the creation method so the component is listed in the details panels
		MeshSplitComponent->CreationMethod = EComponentCreationMethod::Instance;

		bCreatedNewComponent = true;
	}

	if (!MeshSplitComponent)
		return false;

	MeshSplitComponent->SetStaticMesh(InstancedStaticMesh);
	MeshSplitComponent->SetOverrideMaterials(InInstancerMaterials);

	// Now add the instances
	MeshSplitComponent->SetInstanceTransforms(InstancedObjectTransforms);

	// Check for instance colors
	TArray<FLinearColor> InstanceColorOverrides;
	bool ColorOverrideAttributeFound = false;

	// Look for the unreal_instance_color attribute on points	
	HAPI_AttributeInfo AttributeInfo;
	FHoudiniApi::AttributeInfo_Init(&AttributeInfo);
	if (HAPI_RESULT_SUCCESS == FHoudiniApi::GetAttributeInfo(
		FHoudiniEngine::Get().GetSession(), InstancerGeoPartObject.GeoId, InstancerGeoPartObject.PartId,
		HAPI_UNREAL_ATTRIB_INSTANCE_COLOR, HAPI_AttributeOwner::HAPI_ATTROWNER_POINT, &AttributeInfo))
	{
		ColorOverrideAttributeFound = AttributeInfo.exists;
	}
	
	// Look for the unreal_instance_color attribute on prims? (why? original code)
	if (!ColorOverrideAttributeFound)
	{
		if (HAPI_RESULT_SUCCESS == FHoudiniApi::GetAttributeInfo(
			FHoudiniEngine::Get().GetSession(), InstancerGeoPartObject.GeoId, InstancerGeoPartObject.PartId,
			HAPI_UNREAL_ATTRIB_INSTANCE_COLOR, HAPI_AttributeOwner::HAPI_ATTROWNER_PRIM, &AttributeInfo))
		{
			ColorOverrideAttributeFound = AttributeInfo.exists;
		}
	}

	if (ColorOverrideAttributeFound)
	{
		if (AttributeInfo.tupleSize == 4)
		{
			// Allocate sufficient buffer for data.
			InstanceColorOverrides.SetNumZeroed(AttributeInfo.count);

			if (HAPI_RESULT_SUCCESS != FHoudiniApi::GetAttributeFloatData(
				FHoudiniEngine::Get().GetSession(), InstancerGeoPartObject.GeoId, InstancerGeoPartObject.PartId,
				HAPI_UNREAL_ATTRIB_INSTANCE_COLOR, &AttributeInfo, -1, (float*)InstanceColorOverrides.GetData(), 0, AttributeInfo.count))
			{
				InstanceColorOverrides.Empty();
			}
		}
		else if (AttributeInfo.tupleSize == 3)
		{
			// Allocate sufficient buffer for data.
			TArray<float> FloatValues;			
			FloatValues.SetNumZeroed(AttributeInfo.count * AttributeInfo.tupleSize);
			if (HAPI_RESULT_SUCCESS == FHoudiniApi::GetAttributeFloatData(
				FHoudiniEngine::Get().GetSession(), InstancerGeoPartObject.GeoId, InstancerGeoPartObject.PartId,
				HAPI_UNREAL_ATTRIB_INSTANCE_COLOR, &AttributeInfo, -1, (float*)FloatValues.GetData(), 0, AttributeInfo.count))
			{

				// Allocate sufficient buffer for data.
				InstanceColorOverrides.SetNumZeroed(AttributeInfo.count);

				// Convert float to FLinearColors
				for (int32 ColorIdx = 0; ColorIdx < InstanceColorOverrides.Num(); ColorIdx++)
				{
					InstanceColorOverrides[ColorIdx].R = FloatValues[ColorIdx * AttributeInfo.tupleSize + 0];
					InstanceColorOverrides[ColorIdx].G = FloatValues[ColorIdx * AttributeInfo.tupleSize + 1];
					InstanceColorOverrides[ColorIdx].B = FloatValues[ColorIdx * AttributeInfo.tupleSize + 2];
					InstanceColorOverrides[ColorIdx].A = 1.0;
				}
				FloatValues.Empty();
			}
		}
		else
		{
			HOUDINI_LOG_WARNING(TEXT(HAPI_UNREAL_ATTRIB_INSTANCE_COLOR " must be a float[4] or float[3] prim/point attribute"));
		}
	}

	// if we have vertex color overrides, apply them now
#if WITH_EDITOR
	if (InstanceColorOverrides.Num() > 0)
	{
		// Convert the color attribute to FColor
		TArray<FColor> InstanceColors;
		InstanceColors.SetNumUninitialized(InstanceColorOverrides.Num());
		for (int32 ix = 0; ix < InstanceColors.Num(); ++ix)
		{
			InstanceColors[ix] = InstanceColorOverrides[ix].GetClamped().ToFColor(false);
		}

		// Apply them to the instances
		TArray<class UStaticMeshComponent*>& Instances = MeshSplitComponent->GetInstancesForWrite();
		for (int32 InstIndex = 0; InstIndex < Instances.Num(); InstIndex++)
		{
			UStaticMeshComponent* CurSMC = Instances[InstIndex];
			if (!CurSMC || CurSMC->IsPendingKill())
				continue;

			if (!InstanceColors.IsValidIndex(InstIndex))
				continue;

			MeshPaintHelpers::FillStaticMeshVertexColors(CurSMC, -1, InstanceColors[InstIndex], FColor::White);

			//CurSMC->UnregisterComponent();
			//CurSMC->ReregisterComponent();

			{
				// We're only changing instanced vertices on this specific mesh component, so we
				// only need to detach our mesh component
				FComponentReregisterContext ComponentReregisterContext(CurSMC);
				for (auto& CurLODData : CurSMC->LODData)
				{
					BeginInitResource(CurLODData.OverrideVertexColors);
				}
			}

			//FIXME: How to get rid of the warning about fixup vertex colors on load?
			//SMC->FixupOverrideColorsIfNecessary();
		}
	}
#endif

	// Apply generic attributes if we have any
	// TODO: Handle variations w/ index
	// TODO: Optimize
	// Loop on attributes first, then components,
	// if failing to find the attrib on a component, skip the rest
	if (AllPropertyAttributes.Num() > 0)
	{
		TArray<class UStaticMeshComponent*>& Instances = MeshSplitComponent->GetInstancesForWrite();
		for (int32 InstIndex = 0; InstIndex < Instances.Num(); InstIndex++)
		{
			UStaticMeshComponent* CurSMC = Instances[InstIndex];
			if (!CurSMC || CurSMC->IsPendingKill())
				continue;

			for (const auto& CurrentAttrib : AllPropertyAttributes)
			{
				UpdateGenericPropertiesAttributes(CurSMC, AllPropertyAttributes, InstIndex);
			}
		}
	}

	// Assign the new ISMC / HISMC to the output component if we created a new one
	if (bCreatedNewComponent)
		CreatedInstancedComponent = MeshSplitComponent;

	// TODO:
	// We want to make this invisible if it's a collision instancer.
	//CreatedInstancedComponent->SetVisibility(!InstancerGeoPartObject.bIsCollidable);

	return true;
}

bool
FHoudiniInstanceTranslator::CreateOrUpdateStaticMeshComponent(
	UStaticMesh* InstancedStaticMesh,
	const TArray<FTransform>& InstancedObjectTransforms,
	const TArray<FHoudiniGenericAttribute>& AllPropertyAttributes,
	const FHoudiniGeoPartObject& InstancerGeoPartObject,
	USceneComponent* ParentComponent,
	USceneComponent*& CreatedInstancedComponent,
	UMaterialInterface * InstancerMaterial /*=nullptr*/)
{
	if (!InstancedStaticMesh)
		return false;

	if (!ParentComponent || ParentComponent->IsPendingKill())
		return false;

	UObject* ComponentOuter = ParentComponent;
	if (ParentComponent->GetOwner() && !ParentComponent->GetOwner()->IsPendingKill())
		ComponentOuter = ParentComponent->GetOwner();

	bool bCreatedNewComponent = false;
	UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(CreatedInstancedComponent);
	if (!SMC || SMC->IsPendingKill())
	{
		// Create a new StaticMeshComponent
		SMC = NewObject<UStaticMeshComponent>(
			ComponentOuter, UStaticMeshComponent::StaticClass(), NAME_None, RF_Transactional);

		// Change the creation method so the component is listed in the details panels
		SMC->CreationMethod = EComponentCreationMethod::Instance;

		bCreatedNewComponent = true;
	}

	if (!SMC)
		return false;

	SMC->SetStaticMesh(InstancedStaticMesh);
	SMC->GetBodyInstance()->bAutoWeld = false;

	SMC->OverrideMaterials.Empty();
	if (InstancerMaterial)
	{
		int32 MeshMaterialCount = InstancedStaticMesh->StaticMaterials.Num();
		for (int32 Idx = 0; Idx < MeshMaterialCount; ++Idx)
			SMC->SetMaterial(Idx, InstancerMaterial);
	}

	// Now add the instances Transform
	SMC->SetRelativeTransform(InstancedObjectTransforms[0]);

	// Apply generic attributes if we have any
	// TODO: Handle variations w/ index
	for (const auto& CurrentAttrib : AllPropertyAttributes)
	{
		UpdateGenericPropertiesAttributes(SMC, AllPropertyAttributes, 0);
	}

	// Assign the new ISMC / HISMC to the output component if we created a new one
	if (bCreatedNewComponent)
		CreatedInstancedComponent = SMC;

	// TODO:
	// We want to make this invisible if it's a collision instancer.
	//CreatedInstancedComponent->SetVisibility(!InstancerGeoPartObject.bIsCollidable);

	return true;
}

bool
FHoudiniInstanceTranslator::CreateOrUpdateHoudiniStaticMeshComponent(
	UHoudiniStaticMesh* InstancedProxyStaticMesh,
	const TArray<FTransform>& InstancedObjectTransforms,
	const TArray<FHoudiniGenericAttribute>& AllPropertyAttributes,
	const FHoudiniGeoPartObject& InstancerGeoPartObject,
	USceneComponent* ParentComponent,
	USceneComponent*& CreatedInstancedComponent,
	UMaterialInterface * InstancerMaterial /*=nullptr*/)
{
	if (!InstancedProxyStaticMesh)
		return false;

	if (!ParentComponent || ParentComponent->IsPendingKill())
		return false;

	UObject* ComponentOuter = ParentComponent;
	if (ParentComponent->GetOwner() && !ParentComponent->GetOwner()->IsPendingKill())
		ComponentOuter = ParentComponent->GetOwner();

	bool bCreatedNewComponent = false;
	UHoudiniStaticMeshComponent* HSMC = Cast<UHoudiniStaticMeshComponent>(CreatedInstancedComponent);
	if (!HSMC || HSMC->IsPendingKill())
	{
		// Create a new StaticMeshComponent
		HSMC = NewObject<UHoudiniStaticMeshComponent>(
			ComponentOuter, UHoudiniStaticMeshComponent::StaticClass(), NAME_None, RF_Transactional);

		// Change the creation method so the component is listed in the details panels
		HSMC->CreationMethod = EComponentCreationMethod::Instance;

		bCreatedNewComponent = true;
	}

	if (!HSMC)
		return false;

	HSMC->SetMesh(InstancedProxyStaticMesh);
	
	HSMC->OverrideMaterials.Empty();
	if (InstancerMaterial)
	{
		int32 MeshMaterialCount = InstancedProxyStaticMesh->GetNumStaticMaterials();
		for (int32 Idx = 0; Idx < MeshMaterialCount; ++Idx)
			HSMC->SetMaterial(Idx, InstancerMaterial);
	}

	// Now add the instances Transform
	HSMC->SetRelativeTransform(InstancedObjectTransforms[0]);

	// Apply generic attributes if we have any
	// TODO: Handle variations w/ index
	for (const auto& CurrentAttrib : AllPropertyAttributes)
	{
		UpdateGenericPropertiesAttributes(HSMC, AllPropertyAttributes, 0);
	}

	// Assign the new  HSMC to the output component if we created a new one
	if (bCreatedNewComponent)
		CreatedInstancedComponent = HSMC;

	// TODO:
	// We want to make this invisible if it's a collision instancer.
	//CreatedInstancedComponent->SetVisibility(!InstancerGeoPartObject.bIsCollidable);

	return true;
}

bool
FHoudiniInstanceTranslator::HapiGetInstanceTransforms(
	const FHoudiniGeoPartObject& InHGPO, TArray<FTransform>& OutInstancerUnrealTransforms)
{
	// Get the instance transforms	
	int32 PointCount = InHGPO.PartInfo.PointCount;
	if (PointCount <= 0)
		return false;

	TArray<HAPI_Transform> InstanceTransforms;
	InstanceTransforms.SetNum(PointCount);
	for (int32 Idx = 0; Idx < InstanceTransforms.Num(); Idx++)
		FHoudiniApi::Transform_Init(&(InstanceTransforms[Idx]));

	if (HAPI_RESULT_SUCCESS != FHoudiniApi::GetInstanceTransformsOnPart(
		FHoudiniEngine::Get().GetSession(),
		InHGPO.GeoId, InHGPO.PartId, HAPI_SRT,
		&InstanceTransforms[0], 0, PointCount))
	{
		InstanceTransforms.SetNum(0);

		// TODO: Warning? error?
		return false;
	}

	// Convert the transform to Unreal's coordinate system
	OutInstancerUnrealTransforms.SetNumZeroed(InstanceTransforms.Num());
	for (int32 InstanceIdx = 0; InstanceIdx < InstanceTransforms.Num(); InstanceIdx++)
	{
		const auto& InstanceTransform = InstanceTransforms[InstanceIdx];
		FHoudiniEngineUtils::TranslateHapiTransform(InstanceTransform, OutInstancerUnrealTransforms[InstanceIdx]);
	}

	return true;
}

bool
FHoudiniInstanceTranslator::GetGenericPropertiesAttributes(
	const int32& InGeoNodeId, const int32& InPartId, TArray<FHoudiniGenericAttribute>& OutPropertyAttributes)
{
	// List all the generic property detail attributes ...
	int32 FoundCount = FHoudiniEngineUtils::GetGenericAttributeList(
		(HAPI_NodeId)InGeoNodeId, (HAPI_PartId)InPartId, HAPI_UNREAL_ATTRIB_GENERIC_UPROP_PREFIX, OutPropertyAttributes, HAPI_ATTROWNER_DETAIL);

	// .. then get all the values for the primitive property attributes
	FoundCount += FHoudiniEngineUtils::GetGenericAttributeList(
		(HAPI_NodeId)InGeoNodeId, (HAPI_PartId)InPartId, HAPI_UNREAL_ATTRIB_GENERIC_UPROP_PREFIX, OutPropertyAttributes, HAPI_ATTROWNER_PRIM, -1);

	// .. then finally, all values for point uproperty attributes
	// TODO: !! get the correct Index here?
	FoundCount += FHoudiniEngineUtils::GetGenericAttributeList(
		(HAPI_NodeId)InGeoNodeId, (HAPI_PartId)InPartId, HAPI_UNREAL_ATTRIB_GENERIC_UPROP_PREFIX, OutPropertyAttributes, HAPI_ATTROWNER_POINT, -1);

	return FoundCount > 0;
}

bool
FHoudiniInstanceTranslator::UpdateGenericPropertiesAttributes(
	UObject* InObject, const TArray<FHoudiniGenericAttribute>& InAllPropertyAttributes, const int32& AtIndex)
{
	if (!InObject || InObject->IsPendingKill())
		return false;

	// Iterate over the found Property attributes
	int32 NumSuccess = 0;
	for (auto CurrentPropAttribute : InAllPropertyAttributes)
	{
		// Update the current property for the given instance index
		if (!FHoudiniGenericAttribute::UpdatePropertyAttributeOnObject(InObject, CurrentPropAttribute, AtIndex))
			continue;

		// Success!
		NumSuccess++;
		FString ClassName = InObject->GetClass() ? InObject->GetClass()->GetName() : TEXT("Object");
		FString ObjectName = InObject->GetName();
		HOUDINI_LOG_MESSAGE(TEXT("Modified UProperty %s on %s named %s"), *CurrentPropAttribute.AttributeName, *ClassName, *ObjectName);
	}

	return (NumSuccess > 0);
}

bool
FHoudiniInstanceTranslator::RemoveAndDestroyComponent(UObject* InComponent)
{
	if (!InComponent || InComponent->IsPendingKill())
		return false;

	USceneComponent* SceneComponent = Cast<USceneComponent>(InComponent);
	if (SceneComponent && !SceneComponent->IsPendingKill())
	{
		// Remove from the HoudiniAssetActor
		if (SceneComponent->GetOwner())
			SceneComponent->GetOwner()->RemoveOwnedComponent(SceneComponent);

		SceneComponent->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
		SceneComponent->UnregisterComponent();
		SceneComponent->DestroyComponent();

		return true;
	}

	return false;
}


bool
FHoudiniInstanceTranslator::GetMaterialOverridesFromAttributes(
	const int32& InGeoNodeId, const int32& InPartId, TArray<FString>& OutMaterialAttributes)
{
	HAPI_AttributeInfo MaterialAttributeInfo;
	FHoudiniApi::AttributeInfo_Init(&MaterialAttributeInfo);

	FHoudiniEngineUtils::HapiGetAttributeDataAsString(
		InGeoNodeId, InPartId, HAPI_UNREAL_ATTRIB_MATERIAL, MaterialAttributeInfo, OutMaterialAttributes);

	/*
	// TODO: Support material instances on instancers...
	// see FHoudiniMaterialTranslator::CreateMaterialInstances()
	// If material attribute and fallbacks were not found, check the material instance attribute.
	if (!AttribInfoFaceMaterialOverrides.exists)
	{
		PartFaceMaterialOverrides.Empty();
		FHoudiniEngineUtils::HapiGetAttributeDataAsString(
			InGeoNodeId, InPartId, HAPI_UNREAL_ATTRIB_MATERIAL_INSTANCE, MaterialAttributeInfo, OutMaterialAttributes);
	}
	*/

	if (!MaterialAttributeInfo.exists
		/*&& MaterialAttributeInfo.owner != HAPI_ATTROWNER_PRIM
		&& MaterialAttributeInfo.owner != HAPI_ATTROWNER_DETAIL*/)
	{
		HOUDINI_LOG_WARNING(TEXT("Instancer: the unreal_material attribute must be a primitive or detail attribute, ignoring the attribute."));
		OutMaterialAttributes.Empty();
		return false;
	}

	return true;
}

bool
FHoudiniInstanceTranslator::GetInstancerMaterials(
	const int32& InGeoNodeId, const int32& InPartId, TArray<UMaterialInterface*>& OutInstancerMaterials)
{
	TArray<FString> MaterialAttributes;
	if (!GetMaterialOverridesFromAttributes(InGeoNodeId, InPartId, MaterialAttributes))
		MaterialAttributes.Empty();

	bool bHasValidMaterial = false;
	for (auto& CurrentMatString : MaterialAttributes)
	{
		// See if we can find a material interface that matches the attribute
		UMaterialInterface* CurrentMaterialInterface = Cast<UMaterialInterface>(
			StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *CurrentMatString, nullptr, LOAD_NoWarn, nullptr));

		if (!CurrentMaterialInterface || CurrentMaterialInterface->IsPendingKill())
			CurrentMaterialInterface = nullptr;
		else
			bHasValidMaterial = true;
		
		OutInstancerMaterials.Add(CurrentMaterialInterface);
	}

	// IF we couldn't find at least one valid material interface, empty the array
	if (!bHasValidMaterial)
		OutInstancerMaterials.Empty();

	return true;
}


bool
FHoudiniInstanceTranslator::GetVariationMaterials(
	FHoudiniInstancedOutput* InInstancedOutput , const int32& InVariationIndex,
	const TArray<UMaterialInterface*>& InInstancerMaterials, TArray<UMaterialInterface*>& OutVariationMaterials)
{
	if (!InInstancedOutput || InInstancerMaterials.Num() <= 0)
		return false;

	// TODO: This also need to be improved and wont work 100%!!
	// Use the instancedoutputs original object index?
	if(!InInstancedOutput->VariationObjects.IsValidIndex(InVariationIndex))
		return false;
	/*
	// No variations, reuse the array
	if (InInstancedOutput->VariationObjects.Num() == 1)
	{
		OutVariationMaterials = InInstancerMaterials;
		return true;
	}
	*/

	if (InInstancedOutput->TransformVariationIndices.Num() == InInstancerMaterials.Num())
	{
		for (int32 Idx = 0; Idx < InInstancedOutput->TransformVariationIndices.Num(); Idx++)
		{
			int32 VariationAssignment = InInstancedOutput->TransformVariationIndices[Idx];
			if (VariationAssignment != InVariationIndex)
				continue;

			OutVariationMaterials.Add(InInstancerMaterials[Idx]);
		}
	}
	else
	{
		if (InInstancerMaterials.IsValidIndex(InVariationIndex))
			OutVariationMaterials.Add(InInstancerMaterials[InVariationIndex]);
		else
			OutVariationMaterials.Add(InInstancerMaterials[0]);
	}

	return true;
}

bool
FHoudiniInstanceTranslator::IsSplitInstancer(const int32& InGeoId, const int32& InPartId)
{
	bool bSplitMeshInstancer = false;
	bSplitMeshInstancer = FHoudiniEngineUtils::HapiCheckAttributeExists(
		InGeoId, InPartId, HAPI_UNREAL_ATTRIB_SPLIT_INSTANCES, HAPI_ATTROWNER_DETAIL);

	if (!bSplitMeshInstancer)
		return false;

	HAPI_AttributeInfo AttributeInfo;
	FHoudiniApi::AttributeInfo_Init(&AttributeInfo);
	for (int32 AttrIdx = HAPI_ATTROWNER_PRIM; AttrIdx < HAPI_ATTROWNER_MAX; ++AttrIdx)
	{
		HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::GetAttributeInfo(
			FHoudiniEngine::Get().GetSession(),
			InGeoId, InPartId, HAPI_UNREAL_ATTRIB_SPLIT_INSTANCES,
			(HAPI_AttributeOwner)AttrIdx, &AttributeInfo), false);

		if (AttributeInfo.exists)
			break;
	}

	if (!AttributeInfo.exists || AttributeInfo.count <= 0)
		return false;
	
	TArray<int32> IntData;
	// Allocate sufficient buffer for data.
	IntData.SetNumZeroed(AttributeInfo.count * AttributeInfo.tupleSize);
	HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::GetAttributeIntData(
		FHoudiniEngine::Get().GetSession(),
		InGeoId, InPartId, HAPI_UNREAL_ATTRIB_SPLIT_INSTANCES,
		&AttributeInfo, -1, &IntData[0], 0, AttributeInfo.count), false);

	return (IntData[0] != 0);
}


#undef LOCTEXT_NAMESPACE