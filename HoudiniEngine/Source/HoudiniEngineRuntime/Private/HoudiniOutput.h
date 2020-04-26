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

#pragma once

#include "HoudiniGeoPartObject.h"
#include "LandscapeProxy.h"

#include "HoudiniOutput.generated.h"

class UMaterialInterface;

UENUM()
enum class EHoudiniOutputType : uint8
{
	Invalid,

	Mesh,
	Instancer,
	Landscape,
	Curve,
	Skeletal
};

UENUM()
enum class EHoudiniCurveOutputType : uint8
{
	UnrealSpline,
	HoudiniSpline,
};

UENUM()
enum class EHoudiniLandscapeOutputBakeType : uint8
{
	Detachment,
	BakeToImage,
	BakeToWorld,
	InValid,
};

USTRUCT()
struct HOUDINIENGINERUNTIME_API FHoudiniCurveOutputProperties
{
	GENERATED_USTRUCT_BODY()

	// Curve output properties
	UPROPERTY()
	EHoudiniCurveOutputType CurveOutputType = EHoudiniCurveOutputType::HoudiniSpline;

	UPROPERTY()
	int32 NumPoints = -1;

	UPROPERTY()
	bool bClosed = false;

	UPROPERTY()
	EHoudiniCurveType CurveType = EHoudiniCurveType::Invalid;

	UPROPERTY()
	EHoudiniCurveMethod CurveMethod = EHoudiniCurveMethod::Invalid;
};

UCLASS()
class HOUDINIENGINERUNTIME_API UHoudiniLandscapePtr : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	FORCEINLINE
	void SetSoftPtr(TSoftObjectPtr<ALandscapeProxy> InSoftPtr) { LandscapeSoftPtr = InSoftPtr; };

	FORCEINLINE
	TSoftObjectPtr<ALandscapeProxy> GetSoftPtr() const { return LandscapeSoftPtr; };

	FORCEINLINE
	ALandscapeProxy* GetRawPtr() { return Cast<ALandscapeProxy>(LandscapeSoftPtr.Get()); };

	FORCEINLINE
	FString GetSoftPtrPath() const { return LandscapeSoftPtr.ToSoftObjectPath().ToString(); };

	FORCEINLINE
	bool IsWorldCompositionLandscape() const { return bIsWorldCompositionLandscape; };

	FORCEINLINE
	void SetIsWorldCompositionLandscape(const bool& bInIsWorldComposition) { bIsWorldCompositionLandscape = bInIsWorldComposition; };

	FORCEINLINE
	void SetLandscapeOutputBakeType(const EHoudiniLandscapeOutputBakeType & InBakeType) { BakeType = InBakeType; };

	FORCEINLINE
	EHoudiniLandscapeOutputBakeType GetLandscapeOutputBakeType() const { return BakeType; };

	UPROPERTY()
	TSoftObjectPtr<ALandscapeProxy> LandscapeSoftPtr;

	UPROPERTY()
	bool bIsWorldCompositionLandscape;

	UPROPERTY()
	EHoudiniLandscapeOutputBakeType BakeType;
};

USTRUCT()
struct HOUDINIENGINERUNTIME_API FHoudiniOutputObjectIdentifier
{
	GENERATED_USTRUCT_BODY()

public:
	// Constructors
	FHoudiniOutputObjectIdentifier();
	FHoudiniOutputObjectIdentifier(const int32& InObjectId, const int32& InGeoId, const int32& InPartId, const FString& InSplitIdentifier);

	// Return hash value for this object, used when using this object as a key inside hashing containers.
	uint32 GetTypeHash() const;

	// Comparison operator, used by hashing containers.
	bool operator==(const FHoudiniOutputObjectIdentifier& HoudiniGeoPartObject) const;

	bool Matches(const FHoudiniGeoPartObject& HoudiniGeoPartObject) const;

public:

	// NodeId of corresponding Houdini Object.
	UPROPERTY()
	int32 ObjectId = -1;

	// NodeId of corresponding Houdini Geo.
	UPROPERTY()
	int32 GeoId = -1;

	// PartId
	UPROPERTY()
	int32 PartId = -1;

	// String identifier for the split that created this
	UPROPERTY()
	FString SplitIdentifier = FString();

	// Name of the part used to generate the output
	UPROPERTY()
	FString PartName = FString();

	// First valid primitive index for this output
	// (used to read generic attributes)
	UPROPERTY()
	int32 PrimitiveIndex = -1;

	// First valid point index for this output
	// (used to read generic attributes)
	UPROPERTY()
	int32 PointIndex = -1;

	bool bLoaded = false;
};

/** Function used by hashing containers to create a unique hash for this type of object. **/
HOUDINIENGINERUNTIME_API uint32 GetTypeHash(const FHoudiniOutputObjectIdentifier& HoudiniOutputObjectIdentifier);

USTRUCT()
struct HOUDINIENGINERUNTIME_API FHoudiniInstancedOutput
{
	GENERATED_USTRUCT_BODY()

public:

	void MarkChanged(const bool& InChanged) { bChanged = InChanged; };

	void SetVariationObjectAt(const int32& AtIndex, UObject* InObject);

	bool SetTransformOffsetAt(const float& Value, const int32& AtIndex, const int32& PosRotScaleIndex, const int32& XYZIndex);

	float GetTransformOffsetAt(const int32& AtIndex, const int32& PosRotScaleIndex, const int32& XYZIndex);
	
public:

	// Original object used by the instancer.
	UPROPERTY()
	TSoftObjectPtr<UObject> OriginalObject = nullptr;

	UPROPERTY()
	int32 OriginalObjectIndex = -1;

	// Original HoudiniGeoPartObject used by the instancer
	//UPROPERTY()
	//FHoudiniGeoPartObject OriginalHGPO;
	
	// Original Instance transforms
	UPROPERTY()
	TArray<FTransform> OriginalTransforms;

	// Variation objects currently used for instancing
	UPROPERTY()
	TArray<TSoftObjectPtr<UObject>> VariationObjects;

	// Transform offsets, one for each variation.
	UPROPERTY()
	TArray<FTransform> VariationTransformOffsets;

	// Index of the variation used for each transform
	UPROPERTY()
	TArray<int32> TransformVariationIndices;

	// Indicates this instanced output's component should be recreated
	UPROPERTY()
	bool bChanged = false;

	// Indicates this instanced output is stale and should be removed
	UPROPERTY()
	bool bStale = false;

	// TODO
	// Color overrides??
};


USTRUCT()
struct HOUDINIENGINERUNTIME_API FHoudiniOutputObject
{
	GENERATED_USTRUCT_BODY()

	public:

		// The main output object
		UPROPERTY()
		UObject* OutputObject = nullptr;

		// The main output component
		UPROPERTY()
		UObject* OutputComponent = nullptr;

		// Proxy object
		UPROPERTY()
		UObject* ProxyObject = nullptr;

		// Proxy Component
		UPROPERTY()
		UObject* ProxyComponent = nullptr;

		// Mesh output properties
		// If this is true the proxy mesh is "current", 
		// in other words, it is newer than the UStaticMesh
		UPROPERTY()
		bool bProxyIsCurrent = false;

		// Bake Name override for this output object
		UPROPERTY()
		FString BakeName;

		UPROPERTY()
		FHoudiniCurveOutputProperties CurveOutputProperty;
};

UCLASS()
class HOUDINIENGINERUNTIME_API UHoudiniOutput : public UObject
{
	GENERATED_UCLASS_BODY()

	// Declare translators as friend so they can easily directly modify
	// and access our HGPO and Output objects
	friend struct FHoudiniMeshTranslator;
	friend struct FHoudiniInstanceTranslator;

	virtual ~UHoudiniOutput();

public:

	//------------------------------------------------------------------------------------------------
	// Accessors
	//------------------------------------------------------------------------------------------------
	const EHoudiniOutputType& GetType() const { return Type; };

	const TArray<FHoudiniGeoPartObject>& GetHoudiniGeoPartObjects() const {	return HoudiniGeoPartObjects; };

	// Returns true if we have a HGPO that matches
	const bool HasHoudiniGeoPartObject(const FHoudiniGeoPartObject& InHGPO) const;

	// Returns true if the HGPO is fromn the same HF as us
	const bool HeightfieldMatch(const FHoudiniGeoPartObject& InHGPO) const;

	// Returns the output objects and their corresponding identifiers
	TMap<FHoudiniOutputObjectIdentifier, FHoudiniOutputObject>& GetOutputObjects() { return OutputObjects; };

	// Returns this output's assignement material map
	TMap<FString, UMaterialInterface*>& GetAssignementMaterials() { return AssignementMaterials; };
	
	// Returns this output's replacement material map
	TMap<FString, UMaterialInterface*>& GetReplacementMaterials() { return ReplacementMaterials; };

	// Returns the instanced outputs maps
	TMap<FHoudiniOutputObjectIdentifier, FHoudiniInstancedOutput>& GetInstancedOutputs() { return InstancedOutputs; };

	const bool HasGeoChanged() const;
	const bool HasTransformChanged() const;
	const bool HasMaterialsChanged() const;

	// Returns true if there are any proxy objects in output (current or not)
	const bool HasAnyProxy() const;
	// Returns true if the specified identifier has a proxy object (current or not)
	const bool HasProxy(const FHoudiniOutputObjectIdentifier &InIdentifier) const;
	// Returns true if there are any current (most up to date and visible) proxy in the output
	const bool HasAnyCurrentProxy() const;
	// Returns true if the specified identifier's proxy is "current" (in other words, newer than
	// the non-proxy and the proxy should thus be shown instead.
	const bool IsProxyCurrent(const FHoudiniOutputObjectIdentifier &InIdentifier) const;


	//------------------------------------------------------------------------------------------------
	// Mutators
	//------------------------------------------------------------------------------------------------
	void UpdateOutputType();

	// Adds a new HoudiniGeoPartObject to our array
	void AddNewHGPO(const FHoudiniGeoPartObject& InHGPO);

	// Mark all the current HGPO as stale (from a previous cook)
	// So we can delte them all by calling DeleteAllStaleHGPOs after.
	void MarkAllHGPOsAsStale(const bool& InStale);

	// Delete all the HGPO that were marked as stale
	void DeleteAllStaleHGPOs();

	void SetOutputObjects(const TMap<FHoudiniOutputObjectIdentifier, FHoudiniOutputObject>& InOutputObjects) { OutputObjects = InOutputObjects; };

	void SetInstancedOutputs(const TMap<FHoudiniOutputObjectIdentifier, FHoudiniInstancedOutput>& InInstancedOuput) { InstancedOutputs = InInstancedOuput; };

	// Marks all HGPO and OutputIdentifier as loaded
	void MarkAsLoaded(const bool& InLoaded);

	FORCEINLINE
	const bool IsEditableNode() { return bIsEditableNode; };

	FORCEINLINE
	void SetIsEditableNode(bool IsEditable) { bIsEditableNode = IsEditable; }

	FORCEINLINE
	const bool HasEditableNodeBuilt() { return bHasEditableNodeBuilt; };

	FORCEINLINE
	void SetHasEditableNodeBuilt(bool HasBuilt) { bHasEditableNodeBuilt = HasBuilt; }

	FORCEINLINE
	void SetIsUpdating(bool bInIsUpdating) { bIsUpdating = bInIsUpdating; };

	FORCEINLINE
	bool IsUpdating() const { return bIsUpdating; };

	FORCEINLINE
	void SetLandscapeWorldComposition(const bool bInLandscapeWorldComposition) { bLandscapeWorldComposition = bInLandscapeWorldComposition; };
	
	FORCEINLINE
	bool IsLandscapeWorldComposition () const { return bLandscapeWorldComposition; };



	//------------------------------------------------------------------------------------------------
	// Helpers
	//------------------------------------------------------------------------------------------------
	static FString OutputTypeToString(const EHoudiniOutputType& InOutputType);

	// Check if any of the output curve's export type has been changed by the user.
	bool HasCurveExportTypeChanged() const;
	void Clear();

protected:

	virtual void BeginDestroy() override;

protected:

	// Indicates the type of output we're dealing with
	UPROPERTY()
	EHoudiniOutputType Type;

	// The output's corresponding HGPO
	UPROPERTY()
	TArray<FHoudiniGeoPartObject> HoudiniGeoPartObjects;

	//
	UPROPERTY(DuplicateTransient)
	TMap<FHoudiniOutputObjectIdentifier, FHoudiniOutputObject> OutputObjects;

	// Instanced outputs
	// Stores the instance variations objects (replacement), transform offsets 
	UPROPERTY()
	TMap<FHoudiniOutputObjectIdentifier, FHoudiniInstancedOutput> InstancedOutputs;

	// The material assignments for this output
	UPROPERTY()
	TMap<FString, UMaterialInterface*> AssignementMaterials;

	UPROPERTY()
	TMap<FString, UMaterialInterface*> ReplacementMaterials;

	// Indicates the number of stale HGPO
	int32 StaleCount;

	UPROPERTY()
	bool bLandscapeWorldComposition;

private:
	// Use HoudiniOutput to represent an editable curve.
	// This flag tells whether this output is an editable curve.
	UPROPERTY()
	bool bIsEditableNode;

	// An editable node is only built once. This flag indicates whether this node has been built.
	UPROPERTY()
	bool bHasEditableNodeBuilt;

	// The IsUpdating flag is set to true when this out exists and is being updated.
	UPROPERTY()
	bool bIsUpdating;

};


