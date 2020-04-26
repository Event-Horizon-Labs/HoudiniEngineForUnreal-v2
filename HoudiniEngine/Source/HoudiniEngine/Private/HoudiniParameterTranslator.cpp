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

#include "HoudiniParameterTranslator.h"

#include "HoudiniApi.h"
#include "HoudiniEnginePrivatePCH.h"

#include "HoudiniParameter.h"
#include "HoudiniParameterButton.h"
#include "HoudiniParameterChoice.h"
#include "HoudiniParameterColor.h"
#include "HoudiniParameterFile.h"
#include "HoudiniParameterFloat.h"
#include "HoudiniParameterFolder.h"
#include "HoudiniParameterFolderList.h"
#include "HoudiniParameterInt.h"
#include "HoudiniParameterLabel.h"
#include "HoudiniParameterMultiparm.h"
#include "HoudiniParameterRamp.h"
#include "HoudiniParameterSeparator.h"
#include "HoudiniParameterString.h"
#include "HoudiniParameterToggle.h"
#include "HoudiniParameterFile.h"
#include "HoudiniParameterOperatorPath.h"

#include "HoudiniInput.h"

#include "HoudiniEngine.h"
#include "HoudiniEngineUtils.h"
#include "HoudiniEngineString.h"
#include "HoudiniParameter.h"
#include "HoudiniAssetComponent.h"


// Used parameter tags
#define HAPI_PARAM_TAG_NOSWAP						"hengine_noswap"
#define HAPI_PARAM_TAG_FILE_READONLY	            "filechooser_mode"
#define HAPI_PARAM_TAG_UNITS						"units"
#define HAPI_PARAM_TAG_ASSET_REF					"asset_ref"

// Default values for certain UI min and max parameter values
#define HAPI_UNREAL_PARAM_INT_UI_MIN				0
#define HAPI_UNREAL_PARAM_INT_UI_MAX				10
#define HAPI_UNREAL_PARAM_FLOAT_UI_MIN				0.0f
#define HAPI_UNREAL_PARAM_FLOAT_UI_MAX				10.0f

// Some default parameter name
#define HAPI_UNREAL_PARAM_TRANSLATE					"t"
#define HAPI_UNREAL_PARAM_ROTATE					"r"
#define HAPI_UNREAL_PARAM_SCALE						"s"
#define HAPI_UNREAL_PARAM_PIVOT						"p"
#define HAPI_UNREAL_PARAM_UNIFORMSCALE				"scale"

// 
bool 
FHoudiniParameterTranslator::UpdateParameters(UHoudiniAssetComponent* HAC)
{
	if (!HAC || HAC->IsPendingKill())
		return false;

	TArray<UHoudiniParameter*> NewParameters;
	if (FHoudiniParameterTranslator::BuildAllParameters(HAC->GetAssetId(), HAC, HAC->Parameters, NewParameters, true))
	{
		/*
		// DO NOT MANUALLY DESTROY THE OLD/DANGLING PARAMETERS!
		// This messes up unreal's Garbage collection and would cause crashes on duplication

		// Destroy old/dangling parameters
		for (auto& OldParm : HAC->Parameters)
		{
			if (!OldParm || OldParm->IsPendingKill())
				continue;

			OldParm->ConditionalBeginDestroy();
			OldParm = nullptr;
		}
		*/

		// Replace with the new parameters
		HAC->Parameters = NewParameters;
	}


	return true;
}

// 
bool
FHoudiniParameterTranslator::UpdateLoadedParameters(UHoudiniAssetComponent* HAC)
{
	if (!HAC || HAC->IsPendingKill())
		return false;

	// Update all the parameters using the loaded parameter object
	// We set "UpdateValues" to false because we do not want to "read" the parameter value from Houdini
	// but keep the loaded value

	// This is the first cook on loading after a save or duplication, 
	// We need to sync the Ramp parameters first, so that their child parameters can be kept
	// TODO: Do the same thing with multiparms?
	// TODO: Simplify this, should be handled in BuildAllParameters,
	for (int32 Idx = 0; Idx < HAC->Parameters.Num(); ++Idx)
	{
		UHoudiniParameter* Param = HAC->Parameters[Idx];

		if (!Param || Param->IsPendingKill())
			continue;

		switch(Param->GetParameterType())
		{
			case EHoudiniParameterType::ColorRamp:
			case EHoudiniParameterType::FloatRamp:
			case EHoudiniParameterType::MultiParm:
			{
				SyncMultiParmValuesAtLoad(Param, HAC->Parameters, HAC->AssetId, Idx);
			}
			break;

			default:
				break;
		}
	}

	// This call to BuildAllParameters will keep all the loaded parameters (in the HAC's Parameters array)
	// that are still present in the HDA, and keep their loaded value.
	TArray<UHoudiniParameter*> NewParameters;
	if (FHoudiniParameterTranslator::BuildAllParameters(HAC->GetAssetId(), HAC, HAC->Parameters, NewParameters, false))
	{
		/*
		// DO NOT DESTROY OLD PARAMS MANUALLY HERE
		// This causes crashes upon duplication due to uncollected zombie objects...
		// GC is supposed to handle this by itself
		// Destroy old/dangling parameters
		for (auto& OldParm : HAC->Parameters)
		{
			if (!OldParm || OldParm->IsPendingKill())
				continue;

			OldParm->ConditionalBeginDestroy();
			OldParm = nullptr;
		}
		*/

		// Simply replace with the new parameters
		HAC->Parameters = NewParameters;
	}

	return true;
}

bool
FHoudiniParameterTranslator::BuildAllParameters(
	const HAPI_NodeId& AssetId, 
	class UObject* Outer,
	TArray<UHoudiniParameter*>& CurrentParameters,
	TArray<UHoudiniParameter*>& NewParameters,
	const bool& bUpdateValues )
{
	// Ensure the asset has a valid node ID
	if (AssetId < 0)
	{	
		return false;
	}

	// Get the asset's info
	HAPI_AssetInfo AssetInfo;
	HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::GetAssetInfo(
		FHoudiniEngine::Get().GetSession(), AssetId, &AssetInfo), false);

	// .. the asset's node info
	HAPI_NodeInfo NodeInfo;
	HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::GetNodeInfo(
		FHoudiniEngine::Get().GetSession(), AssetInfo.nodeId, &NodeInfo), false);

	NewParameters.Empty();	
	if (NodeInfo.parmCount == 0)
	{
		// The asset doesnt have any parameter, we're done.
		return true;
	}
	else if (NodeInfo.parmCount < 0)
	{
		// Invalid parm count
		return false;
	}

	TArray<int32> AllMultiParams;

	// Retrieve all the parameter infos.
	TArray< HAPI_ParmInfo > ParmInfos;
	ParmInfos.SetNumUninitialized(NodeInfo.parmCount);
	HOUDINI_CHECK_ERROR_RETURN( FHoudiniApi::GetParameters(
			FHoudiniEngine::Get().GetSession(), AssetInfo.nodeId, &ParmInfos[0], 0,	NodeInfo.parmCount), false);

	// Create a name lookup cache for the current parameters
	TMap<FString, UHoudiniParameter*> CurrentParametersByName;
	CurrentParametersByName.Reserve(CurrentParameters.Num());
	for (auto& Parm : CurrentParameters)
	{
		if (!Parm)
			continue;
		CurrentParametersByName.Add(Parm->GetParameterName(), Parm);
	}

	// Create properties for parameters.
	TArray<HAPI_ParmId> NewParmIds;
	for (int32 ParamIdx = 0; ParamIdx < NodeInfo.parmCount; ++ParamIdx)
	{
		
		// Retrieve param info at this index.
		const HAPI_ParmInfo & ParmInfo = ParmInfos[ParamIdx];

		// If the parameter is corrupt, skip it
		if (ParmInfo.id < 0 || ParmInfo.childIndex < 0)
		{
			HOUDINI_LOG_WARNING(TEXT("Corrupt parameter %d detected, skipping."), ParamIdx);
			continue;
		}
		
		// If the parameter is invisible, skip it.
		//if (ParmInfo.invisible)
		//	continue;
		
		// Check if any parent folder of this parameter is invisible 
		bool SkipParm = false;
		HAPI_ParmId ParentId = ParmInfo.parentId;
		while (ParentId > 0 && !SkipParm)
		{
			if (const HAPI_ParmInfo* ParentInfoPtr = ParmInfos.FindByPredicate([=](const HAPI_ParmInfo& Info) {
				return Info.id == ParentId;
			}))
			{
				if (ParentInfoPtr->invisible && ParentInfoPtr->type == HAPI_PARMTYPE_FOLDER)
					SkipParm = true;
				ParentId = ParentInfoPtr->parentId;
			}
			else
			{
				HOUDINI_LOG_ERROR(TEXT("Could not find parent of parameter %d"), ParmInfo.id);
				SkipParm = true;
			}
		}

		if (SkipParm)
			continue;
		
		// See if this parameter has already been created.
		// We can't use the HAPI_ParmId because it is not unique to parameter instances,
		// so instead, try to find the existing parameter by name using the lookup table
		FString NewParmName;
		FHoudiniEngineString(ParmInfo.nameSH).ToFString(NewParmName);

		EHoudiniParameterType ParmType = EHoudiniParameterType::Invalid;
		FHoudiniParameterTranslator::GetParmTypeFromParmInfo(ParmInfo, ParmType);

		UHoudiniParameter ** FoundHoudiniParameter = CurrentParametersByName.Find(NewParmName);

		// If that parameter exists, we might be able to simply reuse it.
		bool IsFoundParameterValid = false;
		if (FoundHoudiniParameter && *FoundHoudiniParameter && !(*FoundHoudiniParameter)->IsPendingKill())
		{
			// First, we can simply check that the tuple size hasn't changed
			if ((*FoundHoudiniParameter)->GetTupleSize() != ParmInfo.size)
			{
				IsFoundParameterValid = false;
			}
			else if (ParmType == EHoudiniParameterType::Invalid )
			{
				IsFoundParameterValid = false;
			}					
			else if (ParmType != (*FoundHoudiniParameter)->GetParameterType() )
			{
				// Types do not match
				IsFoundParameterValid = false;
			}
			else if ( !CheckParameterTypeAndClassMatch( *FoundHoudiniParameter, ParmType) )
			{
				// Found parameter class does not match
				IsFoundParameterValid = false;
			}
			else
			{
				// We can reuse the parameter
				IsFoundParameterValid = true;
			}
		}
		
		UHoudiniParameter * HoudiniAssetParameter = nullptr;
		
		if (IsFoundParameterValid)
		{
			// We can reuse the parameter we found
			HoudiniAssetParameter = *FoundHoudiniParameter;

			// Transfer param object from current map to new map
			CurrentParameters.Remove(HoudiniAssetParameter);
			CurrentParametersByName.Remove(NewParmName);

			// Do a fast update of this parameter
			if (!FHoudiniParameterTranslator::UpdateParameterFromInfo(HoudiniAssetParameter, AssetInfo.nodeId, ParmInfo, false, bUpdateValues))
				continue;

			// Reset the states of ramp parameters.
			switch (HoudiniAssetParameter->GetParameterType())
			{

				case EHoudiniParameterType::FloatRamp:
				{
					UHoudiniParameterRampFloat* FloatRampParam = Cast<UHoudiniParameterRampFloat>(HoudiniAssetParameter);
					if (FloatRampParam)
					{
						UHoudiniAssetComponent* ParentHAC = Cast<UHoudiniAssetComponent>(FloatRampParam->GetOuter());
						if (ParentHAC && !ParentHAC->HasBeenLoaded() && !ParentHAC->HasBeenDuplicated())
							FloatRampParam->bCaching = false;
					}

					break;
				}

				case EHoudiniParameterType::ColorRamp:
				{
					UHoudiniParameterRampColor* ColorRampParam = Cast<UHoudiniParameterRampColor>(HoudiniAssetParameter);
					if (ColorRampParam)
					{
						UHoudiniAssetComponent* ParentHAC = Cast<UHoudiniAssetComponent>(ColorRampParam->GetOuter());
						if (ParentHAC && !ParentHAC->HasBeenLoaded() && !ParentHAC->HasBeenDuplicated())
							ColorRampParam->bCaching = false;
					}

					break;
				}
			}

		}
		else
		{	
			// Create a new parameter object of the appropriate type
			HoudiniAssetParameter = CreateTypedParameter(Outer, ParmType, NewParmName);
			// Fully update this parameter
			if (!FHoudiniParameterTranslator::UpdateParameterFromInfo(HoudiniAssetParameter, AssetInfo.nodeId, ParmInfo, true, true))
				continue;

		}
		
		// Add the new parameters
		NewParameters.Add(HoudiniAssetParameter);
		NewParmIds.Add(ParmInfo.id);


		// Check if the parameter is a direct child of a multiparam.
		if (HoudiniAssetParameter->GetParameterType() == EHoudiniParameterType::MultiParm)
			AllMultiParams.Add(HoudiniAssetParameter->GetParmId());

		if (AllMultiParams.Contains(HoudiniAssetParameter->GetParentParmId()))
		{
			HoudiniAssetParameter->SetIsDirectChildOfMultiParm(true);

			// Treat the folderlist whose direct parent is a multi param as a multi param too.
			if (HoudiniAssetParameter->GetParameterType() == EHoudiniParameterType::FolderList)
				AllMultiParams.Add(HoudiniAssetParameter->GetParmId());
		}

	}

	FHoudiniEngineUtils::UpdateEditorProperties(Outer, true);

	return true;
}


void
FHoudiniParameterTranslator::GetParmTypeFromParmInfo(
	const HAPI_ParmInfo& ParmInfo,
	EHoudiniParameterType& ParmType)
{
	ParmType = EHoudiniParameterType::Invalid;
	//ParmValueType = EHoudiniParameterValueType::Invalid;

	switch (ParmInfo.type)
	{
		case HAPI_PARMTYPE_BUTTON:
			ParmType = EHoudiniParameterType::Button;
			//ParmValueType = EHoudiniParameterValueType::Int;
			break;

		case HAPI_PARMTYPE_STRING:
		{
			if (ParmInfo.choiceCount > 0)
			{
				ParmType = EHoudiniParameterType::StringChoice;
				//ParmValueType = EHoudiniParameterValueType::String;
			}
			else
			{
				ParmType = EHoudiniParameterType::String;
				//ParmValueType = EHoudiniParameterValueType::String;
			}
			break;
		}

		case HAPI_PARMTYPE_INT:
		{
			if (ParmInfo.choiceCount > 0)
			{
				ParmType = EHoudiniParameterType::IntChoice;
				//ParmValueType = EHoudiniParameterValueType::Int;
			}
			else
			{
				ParmType = EHoudiniParameterType::Int;
				//ParmValueType = EHoudiniParameterValueType::Int;
			}
			break;
		}

		case HAPI_PARMTYPE_FLOAT:
		{
			ParmType = EHoudiniParameterType::Float;
			//ParmValueType = EHoudiniParameterValueType::Float;
			break;
		}

		case HAPI_PARMTYPE_TOGGLE:
		{
			ParmType = EHoudiniParameterType::Toggle;
			//ParmValueType = EHoudiniParameterValueType::Int;
			break;
		}

		case HAPI_PARMTYPE_COLOR:
		{
			ParmType = EHoudiniParameterType::Color;
			//ParmValueType = EHoudiniParameterValueType::Float;
			break;
		}

		case HAPI_PARMTYPE_LABEL:
		{
			ParmType = EHoudiniParameterType::Label;
			//ParmValueType = EHoudiniParameterValueType::String;
			break;
		}

		case HAPI_PARMTYPE_SEPARATOR:
		{
			ParmType = EHoudiniParameterType::Separator;
			//ParmValueType = EHoudiniParameterValueType::None;
			break;
		}

		case HAPI_PARMTYPE_FOLDERLIST:
		{
			ParmType = EHoudiniParameterType::FolderList;
			//ParmValueType = EHoudiniParameterValueType::None;
			break;
		}

		case HAPI_PARMTYPE_FOLDER:
		{
			ParmType = EHoudiniParameterType::Folder;
			//ParmValueType = EHoudiniParameterValueType::None;
			break;
		}

		case HAPI_PARMTYPE_MULTIPARMLIST:
		{
			if (HAPI_RAMPTYPE_FLOAT == ParmInfo.rampType)
			{
				ParmType = EHoudiniParameterType::FloatRamp;
				//ParmValueType = EHoudiniParameterValueType::Float;
			}
			else if (HAPI_RAMPTYPE_COLOR == ParmInfo.rampType)
			{
				ParmType = EHoudiniParameterType::ColorRamp;
				//ParmValueType = EHoudiniParameterValueType::Float;
			}
			else
			{
				ParmType = EHoudiniParameterType::MultiParm;
				//ParmValueType = EHoudiniParameterValueType::Int;
			}
			break;
		}

		case HAPI_PARMTYPE_PATH_FILE:
		{
			ParmType = EHoudiniParameterType::File;
			//ParmValueType = EHoudiniParameterValueType::String;
			break;
		}

		case HAPI_PARMTYPE_PATH_FILE_DIR:
		{
			ParmType = EHoudiniParameterType::FileDir;
			//ParmValueType = EHoudiniParameterValueType::String;
			break;
		}

		case HAPI_PARMTYPE_PATH_FILE_GEO:
		{
			ParmType = EHoudiniParameterType::FileGeo;
			//ParmValueType = EHoudiniParameterValueType::String;
			break;
		}

		case HAPI_PARMTYPE_PATH_FILE_IMAGE:
		{
			ParmType = EHoudiniParameterType::FileImage;
			//ParmValueType = EHoudiniParameterValueType::String;
			break;
		}

		case HAPI_PARMTYPE_NODE:
		{
			if (ParmInfo.inputNodeType == HAPI_NODETYPE_ANY ||
				ParmInfo.inputNodeType == HAPI_NODETYPE_SOP ||
				ParmInfo.inputNodeType == HAPI_NODETYPE_OBJ)
			{
				ParmType = EHoudiniParameterType::Input;
			}
			else
			{
				ParmType = EHoudiniParameterType::String;
			}
			break;
		}

		default:
		{
			// Just ignore unsupported types for now.
			HOUDINI_LOG_WARNING(TEXT("Parameter Type (%d) is unsupported"), static_cast<int32>(ParmInfo.type));
			break;
		}
	}
}

UClass* 
FHoudiniParameterTranslator::GetDesiredParameterClass(const HAPI_ParmInfo& ParmInfo)
{
	UClass* FoundClass = nullptr;

	switch (ParmInfo.type)
	{
	case HAPI_PARMTYPE_STRING:
		if (!ParmInfo.choiceCount)
			FoundClass = UHoudiniParameterString::StaticClass();
		else
			FoundClass = UHoudiniParameterChoice ::StaticClass();
		break;

	case HAPI_PARMTYPE_INT:
		if (!ParmInfo.choiceCount)
			FoundClass = UHoudiniParameterInt::StaticClass();
		else
			FoundClass = UHoudiniParameterChoice::StaticClass();
		break;

	case HAPI_PARMTYPE_FLOAT:
		FoundClass = UHoudiniParameterFloat::StaticClass();
		break;

	case HAPI_PARMTYPE_TOGGLE:
		FoundClass = UHoudiniParameterToggle::StaticClass();
		break;

	case HAPI_PARMTYPE_COLOR:
		FoundClass = UHoudiniParameterColor::StaticClass();
		break;

	case HAPI_PARMTYPE_LABEL:
		FoundClass = UHoudiniParameterLabel::StaticClass();
		break;

	case HAPI_PARMTYPE_BUTTON:
		FoundClass = UHoudiniParameterButton::StaticClass();
		break;

	case HAPI_PARMTYPE_SEPARATOR:
		FoundClass = UHoudiniParameterSeparator::StaticClass();
		break;

	case HAPI_PARMTYPE_FOLDERLIST:
		FoundClass = UHoudiniParameterFolderList::StaticClass();
		break;

	case HAPI_PARMTYPE_FOLDER:
		FoundClass = UHoudiniParameterFolder::StaticClass();
		break;

	case HAPI_PARMTYPE_MULTIPARMLIST:
	{
		if (HAPI_RAMPTYPE_FLOAT == ParmInfo.rampType || HAPI_RAMPTYPE_COLOR == ParmInfo.rampType)
			FoundClass = UHoudiniParameterRampFloat::StaticClass();
		else if (HAPI_RAMPTYPE_COLOR == ParmInfo.rampType)
			FoundClass = UHoudiniParameterRampColor::StaticClass();
	}
		break;

	case HAPI_PARMTYPE_PATH_FILE:
		FoundClass = UHoudiniParameterFile::StaticClass();
		break;
	case HAPI_PARMTYPE_PATH_FILE_DIR:
		FoundClass = UHoudiniParameterFile::StaticClass();
		break;
	case HAPI_PARMTYPE_PATH_FILE_GEO:
		FoundClass = UHoudiniParameterFile::StaticClass();
		break;
	case HAPI_PARMTYPE_PATH_FILE_IMAGE:
		FoundClass = UHoudiniParameterFile::StaticClass();
		break;

	case HAPI_PARMTYPE_NODE:
		if (ParmInfo.inputNodeType == HAPI_NODETYPE_ANY ||
			ParmInfo.inputNodeType == HAPI_NODETYPE_SOP ||
			ParmInfo.inputNodeType == HAPI_NODETYPE_OBJ)
		{
			FoundClass = UHoudiniParameter::StaticClass();
		}
		else
		{
			FoundClass = UHoudiniParameterString::StaticClass();
		}
		break;
	}

	if (FoundClass == nullptr)
		return UHoudiniParameter::StaticClass();

	return FoundClass;
}

bool
FHoudiniParameterTranslator::CheckParameterTypeAndClassMatch(UHoudiniParameter* Parameter, const EHoudiniParameterType& ParmType)
{
	UClass* FoundClass = Parameter->GetClass();
	bool FailedTypeCheck = true;

	switch (ParmType)
	{
		case EHoudiniParameterType::Invalid:
		{
			FailedTypeCheck = true;
			break;
		}

		case EHoudiniParameterType::Button:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterButton >();
			break;
		}

		case EHoudiniParameterType::Color:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterColor >();
			break;
		}

		case EHoudiniParameterType::ColorRamp:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterRampColor >();
			break;
		}
		case EHoudiniParameterType::FloatRamp:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterRampFloat >();
			break;
		}
		
		case EHoudiniParameterType::File:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterFile >();
			break;
		}
		case EHoudiniParameterType::FileDir:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterFile >();
			break;
		}
		case EHoudiniParameterType::FileGeo:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterFile >();
			break;
		}
		case EHoudiniParameterType::FileImage:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterFile >();
			break;
		}
				
		case EHoudiniParameterType::Float:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterFloat >();
			break;
		}
		
		case EHoudiniParameterType::Folder:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterFolder >();
			break;
		}

		case EHoudiniParameterType::FolderList:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterFolderList >();
			break;
		}

		case EHoudiniParameterType::Input:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterOperatorPath >();
			break;
		}

		case EHoudiniParameterType::Int:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterInt >();
			break;
		}

		case EHoudiniParameterType::IntChoice:
		case EHoudiniParameterType::StringChoice:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterChoice >();
			break;
		}

		case EHoudiniParameterType::Label:
		{ 
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterLabel >();
			break;
		}

		case EHoudiniParameterType::MultiParm:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterMultiParm >();
			break;
		}

		case EHoudiniParameterType::Separator:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterSeparator >();
			break; 
		}

		case EHoudiniParameterType::String:
		case EHoudiniParameterType::StringAssetRef:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterString >();
			break;
		}

		case EHoudiniParameterType::Toggle:
		{
			FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterToggle >();
			break;
		}
	};

	return !FailedTypeCheck;
}
/*
bool
FHoudiniParameterTranslator::CheckParameterClassAndInfoMatch(UHoudiniParameter* Parameter, const HAPI_ParmInfo& ParmInfo)
{
	if (!Parameter || Parameter->IsPendingKill())
		return false;

	UClass* FoundClass = Parameter->GetClass();
	bool FailedTypeCheck = true;
	switch (ParmInfo.type)
	{
		case HAPI_PARMTYPE_STRING:
			if (!ParmInfo.choiceCount)
				FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterString >();
			else
				FailedTypeCheck &= !FoundClass->IsChildOf< UHoudiniParameterChoice >();
			break;

		case HAPI_PARMTYPE_INT:
			if (!ParmInfo.choiceCount)
				FailedTypeCheck &= !FoundClass->IsChildOf<UHoudiniParameterInt>();
			else
				FailedTypeCheck &= !FoundClass->IsChildOf<UHoudiniParameterChoice>();
			break;

		case HAPI_PARMTYPE_FLOAT:
			FailedTypeCheck &= !FoundClass->IsChildOf<UHoudiniParameterFloat>();
			break;

		case HAPI_PARMTYPE_TOGGLE:
			FailedTypeCheck &= !FoundClass->IsChildOf<UHoudiniParameterToggle>();
			break;

		case HAPI_PARMTYPE_COLOR:
			FailedTypeCheck &= !FoundClass->IsChildOf<UHoudiniParameterColor>();
			break;

		case HAPI_PARMTYPE_LABEL:
			FailedTypeCheck &= !FoundClass->IsChildOf<UHoudiniParameterLabel>();
			break;

		case HAPI_PARMTYPE_BUTTON:
			FailedTypeCheck &= !FoundClass->IsChildOf<UHoudiniParameterButton>();
			break;

		case HAPI_PARMTYPE_SEPARATOR:
			FailedTypeCheck &= !FoundClass->IsChildOf<UHoudiniParameterSeparator>();
			break;

		case HAPI_PARMTYPE_FOLDERLIST:
			FailedTypeCheck &= !FoundClass->IsChildOf<UHoudiniParameterFolderList>();
			break;

		case HAPI_PARMTYPE_FOLDER:
			FailedTypeCheck &= !FoundClass->IsChildOf<UHoudiniParameterFolder>();
			break;

		case HAPI_PARMTYPE_MULTIPARMLIST:
			if (HAPI_RAMPTYPE_FLOAT == ParmInfo.rampType || HAPI_RAMPTYPE_COLOR == ParmInfo.rampType)
			{
				FailedTypeCheck &= !FoundClass->IsChildOf<UHoudiniParameterRamp>();
			}
			break;

		case HAPI_PARMTYPE_PATH_FILE:
		case HAPI_PARMTYPE_PATH_FILE_DIR:
		case HAPI_PARMTYPE_PATH_FILE_GEO:
		case HAPI_PARMTYPE_PATH_FILE_IMAGE:
			FailedTypeCheck &= !FoundClass->IsChildOf<UHoudiniParameterFile>();
			break;

		case HAPI_PARMTYPE_NODE:
			if (ParmInfo.inputNodeType == HAPI_NODETYPE_ANY ||
				ParmInfo.inputNodeType == HAPI_NODETYPE_SOP ||
				ParmInfo.inputNodeType == HAPI_NODETYPE_OBJ)
			{
				FailedTypeCheck &= !FoundClass->IsChildOf<UHoudiniParameter>();
			}
			else
			{
				FailedTypeCheck &= !FoundClass->IsChildOf<UHoudiniParameterString>();
			}
			break;
	}

	return FailedTypeCheck;
}
*/

UHoudiniParameter *
FHoudiniParameterTranslator::CreateTypedParameter(UObject * Outer, const EHoudiniParameterType& ParmType, const FString& ParmName)
{
	UHoudiniParameter* HoudiniParameter = nullptr;
	// Create a parameter of the desired type
	switch (ParmType)
	{
		case EHoudiniParameterType::Button:
			HoudiniParameter = UHoudiniParameterButton::Create(Outer, ParmName);
			break;

		case EHoudiniParameterType::Color:
			HoudiniParameter = UHoudiniParameterColor::Create(Outer, ParmName);
			break;

		case EHoudiniParameterType::ColorRamp:
			HoudiniParameter = UHoudiniParameterRampColor::Create(Outer, ParmName);
			break;

		case EHoudiniParameterType::FloatRamp:
			HoudiniParameter = UHoudiniParameterRampFloat::Create(Outer, ParmName);
			break;

		case EHoudiniParameterType::File:
			HoudiniParameter = UHoudiniParameterFile::Create(Outer, ParmName);
			break;

		case EHoudiniParameterType::FileDir:
			HoudiniParameter = UHoudiniParameterFile::Create(Outer, ParmName);
			HoudiniParameter->SetParameterType(EHoudiniParameterType::FileDir);
			break;

		case EHoudiniParameterType::FileGeo:
			HoudiniParameter = UHoudiniParameterFile::Create(Outer, ParmName);
			HoudiniParameter->SetParameterType(EHoudiniParameterType::FileGeo);
			break;

		case EHoudiniParameterType::FileImage:
			HoudiniParameter = UHoudiniParameterFile::Create(Outer, ParmName);
			HoudiniParameter->SetParameterType(EHoudiniParameterType::FileImage);
			break;

		case EHoudiniParameterType::Float:
			HoudiniParameter = UHoudiniParameterFloat::Create(Outer, ParmName);
			break;

		case EHoudiniParameterType::Folder:
			HoudiniParameter = UHoudiniParameterFolder::Create(Outer, ParmName);
			break;

		case EHoudiniParameterType::FolderList:
			HoudiniParameter = UHoudiniParameterFolderList::Create(Outer, ParmName);
			break;

		case EHoudiniParameterType::Input:
			// Input parameter simply use the base class as all the processingsince is handled by UHoudiniInput
			HoudiniParameter = UHoudiniParameterOperatorPath::Create(Outer, ParmName);
			HoudiniParameter->SetParameterType(ParmType);
			break;

		case EHoudiniParameterType::Int:
			HoudiniParameter = UHoudiniParameterInt::Create(Outer, ParmName);
			break;

		case EHoudiniParameterType::IntChoice:
			HoudiniParameter = UHoudiniParameterChoice::Create(Outer, ParmName, EHoudiniParameterType::IntChoice);
			break;

		case EHoudiniParameterType::StringChoice:
			HoudiniParameter = UHoudiniParameterChoice::Create(Outer, ParmName, EHoudiniParameterType::StringChoice);
			break;

		case EHoudiniParameterType::Label:
			HoudiniParameter = UHoudiniParameterLabel::Create(Outer, ParmName);
			break;

		case EHoudiniParameterType::MultiParm:
			HoudiniParameter = UHoudiniParameterMultiParm::Create(Outer, ParmName);
			break;

		case EHoudiniParameterType::Separator:
			HoudiniParameter = UHoudiniParameterSeparator::Create(Outer, ParmName);
			break;

		case EHoudiniParameterType::String:
		case EHoudiniParameterType::StringAssetRef:
			HoudiniParameter = UHoudiniParameterString::Create(Outer, ParmName);
			break;

		case EHoudiniParameterType::Toggle:
			HoudiniParameter = UHoudiniParameterToggle::Create(Outer, ParmName);
			break;

		case EHoudiniParameterType::Invalid:
			// TODO handle invalid params
			HoudiniParameter = UHoudiniParameter::Create(Outer, ParmName);
			break;
	}

	return HoudiniParameter;
}

bool
FHoudiniParameterTranslator::UpdateParameterFromInfo(
	UHoudiniParameter * HoudiniParameter, const HAPI_NodeId& InNodeId, const HAPI_ParmInfo& ParmInfo,
	const bool& bFullUpdate, const bool& bUpdateValue)
{
	if (!HoudiniParameter || HoudiniParameter->IsPendingKill())
		return false;

	// Copy values from the ParmInfos
	HoudiniParameter->SetNodeId(InNodeId);
	HoudiniParameter->SetParmId(ParmInfo.id);
	HoudiniParameter->SetParentParmId(ParmInfo.parentId);

	HoudiniParameter->SetChildIndex(ParmInfo.childIndex);
	HoudiniParameter->SetTagCount(ParmInfo.tagCount);
	HoudiniParameter->SetTupleSize(ParmInfo.size);

	HoudiniParameter->SetVisible(!ParmInfo.invisible);
	HoudiniParameter->SetDisabled(ParmInfo.disabled);
	HoudiniParameter->SetSpare(ParmInfo.spare);
	HoudiniParameter->SetJoinNext(ParmInfo.joinNext);

	HoudiniParameter->SetTagCount(ParmInfo.tagCount);
	HoudiniParameter->SetIsChildOfMultiParm(ParmInfo.isChildOfMultiParm);

	UHoudiniParameterMultiParm* MultiParm = Cast<UHoudiniParameterMultiParm>(HoudiniParameter);
	if(MultiParm)
		MultiParm->InstanceStartOffset = ParmInfo.instanceStartOffset;

	

	// Get the parameter type
	EHoudiniParameterType ParmType = HoudiniParameter->GetParameterType();

	// We need to set string values from the parmInfo
	if (bFullUpdate)
	{
		FString Name;
		{
			// Name
			FHoudiniEngineString HoudiniEngineStringName(ParmInfo.nameSH);
			if (HoudiniEngineStringName.ToFString(Name))
				HoudiniParameter->SetParameterName(Name);
		}

		{
			// Label
			FString Label;
			FHoudiniEngineString HoudiniEngineStringLabel(ParmInfo.labelSH);
			if (HoudiniEngineStringLabel.ToFString(Label))
				HoudiniParameter->SetParameterLabel(Label);
		}

		{
			// Help
			FString Help;
			FHoudiniEngineString HoudiniEngineStringHelp(ParmInfo.helpSH);
			if (HoudiniEngineStringHelp.ToFString(Help))
				HoudiniParameter->SetParameterHelp(Help);
		}

		if (ParmType == EHoudiniParameterType::String
			|| ParmType == EHoudiniParameterType::Int
			|| ParmType == EHoudiniParameterType::Float
			|| ParmType == EHoudiniParameterType::Toggle
			|| ParmType == EHoudiniParameterType::Color)
		{
			// See if the parm has an expression
			int32 TupleIdx = ParmInfo.intValuesIndex;
			bool bHasExpression = false;
			if (HAPI_RESULT_SUCCESS != FHoudiniApi::ParmHasExpression(
				FHoudiniEngine::Get().GetSession(), InNodeId,
				TCHAR_TO_UTF8(*Name), TupleIdx, &bHasExpression))
			{
				// ?
			}

			FString ParmExprString = TEXT("");
			if (bHasExpression)
			{
				// Try to get the expression's value
				HAPI_StringHandle StringHandle;
				if (HAPI_RESULT_SUCCESS == FHoudiniApi::GetParmExpression(
					FHoudiniEngine::Get().GetSession(), InNodeId,
					TCHAR_TO_UTF8(*Name), TupleIdx, &StringHandle))
				{
					FHoudiniEngineString HoudiniEngineString(StringHandle);
					HoudiniEngineString.ToFString(ParmExprString);
				}

				// Check if we actually have an expression
				// String parameters return true even if they do not have one
				bHasExpression = ParmExprString.Len() > 0;

			}

			HoudiniParameter->SetHasExpression(bHasExpression);
			HoudiniParameter->SetExpression(ParmExprString);
		}
		else
		{
			HoudiniParameter->SetHasExpression(false);
			HoudiniParameter->SetExpression(FString());
		}
		
		// Get parameter tags.
		int32 TagCount = HoudiniParameter->GetTagCount();
		for (int32 Idx = 0; Idx < TagCount; ++Idx)
		{
			HAPI_StringHandle TagNameSH;
			if (HAPI_RESULT_SUCCESS != FHoudiniApi::GetParmTagName(
				FHoudiniEngine::Get().GetSession(),
				InNodeId, ParmInfo.id, Idx, &TagNameSH))
			{
				HOUDINI_LOG_WARNING(TEXT("Failed to retrive parameter tag name: parmId: %d, tag index: %d"), ParmInfo.id, Idx);
				continue;
			}

			FString NameString = TEXT("");
			FHoudiniEngineString::ToFString(TagNameSH, NameString);
			if (NameString.IsEmpty())
			{
				HOUDINI_LOG_WARNING(TEXT("Failed to retrive parameter tag name: parmId: %d, tag index: %d"), ParmInfo.id, Idx);
				continue;
			}

			HAPI_StringHandle TagValueSH;
			if (HAPI_RESULT_SUCCESS != FHoudiniApi::GetParmTagValue(
				FHoudiniEngine::Get().GetSession(),
				InNodeId, ParmInfo.id, TCHAR_TO_ANSI(*NameString), &TagValueSH))
			{
				HOUDINI_LOG_WARNING(TEXT("Failed to retrive parameter tag value: parmId: %d, tag: %s"), ParmInfo.id, *NameString);
			}

			FString ValueString = TEXT("");
			FHoudiniEngineString::ToFString(TagValueSH, ValueString);

			HoudiniParameter->GetTags().Add(NameString, ValueString);
		}
	}

	//
	// Update properties specific to parameter classes
	switch (ParmType)
	{
		case EHoudiniParameterType::Button:
		{
			UHoudiniParameterButton* HoudiniParameterButton = Cast<UHoudiniParameterButton>(HoudiniParameter);
			if (HoudiniParameterButton && !HoudiniParameterButton->IsPendingKill())
			{
				HoudiniParameterButton->SetValueIndex(ParmInfo.intValuesIndex);
			}
		}
		break;

		case EHoudiniParameterType::Color:
		{
			UHoudiniParameterColor* HoudiniParameterColor = Cast<UHoudiniParameterColor>(HoudiniParameter);
			if (HoudiniParameterColor && !HoudiniParameterColor->IsPendingKill())
			{
				// Set the valueIndex
				HoudiniParameterColor->SetValueIndex(ParmInfo.floatValuesIndex);

				// Update the Parameter value if we want to
				if (bUpdateValue)
				{
					// Get the actual value for this property.
					FLinearColor Color = FLinearColor::White;
					if (FHoudiniApi::GetParmFloatValues(
						FHoudiniEngine::Get().GetSession(), InNodeId,
						(float *)&Color.R, ParmInfo.floatValuesIndex, ParmInfo.size) != HAPI_RESULT_SUCCESS)
					{
						return false;
					}

					HoudiniParameterColor->SetColorValue(Color);
				}
			}
		}
		break;

		case EHoudiniParameterType::ColorRamp:
		{
			UHoudiniParameterRampColor* HoudiniParameterRampColor = Cast<UHoudiniParameterRampColor>(HoudiniParameter);
			if (HoudiniParameterRampColor && !HoudiniParameterRampColor->IsPendingKill())
			{
				HoudiniParameterRampColor->SetInstanceCount(ParmInfo.instanceCount);
				HoudiniParameterRampColor->MultiParmInstanceLength = ParmInfo.instanceLength;
			}
		}
			break;
		case EHoudiniParameterType::FloatRamp:
		{
			UHoudiniParameterRampFloat* HoudiniParameterRampFloat = Cast<UHoudiniParameterRampFloat>(HoudiniParameter);
			if (HoudiniParameterRampFloat && !HoudiniParameterRampFloat->IsPendingKill())
			{
				HoudiniParameterRampFloat->SetInstanceCount(ParmInfo.instanceCount);
				HoudiniParameterRampFloat->MultiParmInstanceLength = ParmInfo.instanceLength;
			}	
		}
		break;

		case EHoudiniParameterType::File:
		case EHoudiniParameterType::FileDir:
		case EHoudiniParameterType::FileGeo:
		case EHoudiniParameterType::FileImage:
		{
			UHoudiniParameterFile* HoudiniParameterFile = Cast<UHoudiniParameterFile>(HoudiniParameter);
			if (HoudiniParameterFile && !HoudiniParameterFile->IsPendingKill())
			{
				// Set the valueIndex
				HoudiniParameterFile->SetValueIndex(ParmInfo.stringValuesIndex);

				// Update the file filter and read only tag only for full updates
				if (bFullUpdate)
				{
					// Check if we are read-only
					bool bIsReadOnly = false;
					FString FileChooserTag;
					if (FHoudiniParameterTranslator::HapiGetParameterTagValue(InNodeId, ParmInfo.id, HAPI_PARAM_TAG_FILE_READONLY, FileChooserTag))
					{
						if (FileChooserTag.Equals(TEXT("read"), ESearchCase::IgnoreCase))
							bIsReadOnly = true;
					}
					HoudiniParameterFile->SetReadOnly(bIsReadOnly);

					// Update the file type using the typeInfo string.
					if (ParmInfo.typeInfoSH > 0)
					{
						FString Filters;
						FHoudiniEngineString HoudiniEngineString(ParmInfo.typeInfoSH);
						if (HoudiniEngineString.ToFString(Filters))
						{
							if (!Filters.IsEmpty())
								HoudiniParameterFile->SetFileFilters(Filters);
						}
					}
				}

				if (bUpdateValue)
				{
					// Get the actual values for this property.
					TArray< HAPI_StringHandle > StringHandles;
					StringHandles.SetNumZeroed(ParmInfo.size);
					if (FHoudiniApi::GetParmStringValues(
						FHoudiniEngine::Get().GetSession(), InNodeId, false,
						&StringHandles[0], ParmInfo.stringValuesIndex, ParmInfo.size) != HAPI_RESULT_SUCCESS)
					{
						return false;
					}

					// Convert HAPI string handles to Unreal strings.
					HoudiniParameterFile->SetNumberOfValues(ParmInfo.size);
					for (int32 Idx = 0; Idx < StringHandles.Num(); ++Idx)
					{
						FString ValueString = TEXT("");
						FHoudiniEngineString HoudiniEngineString(StringHandles[Idx]);
						HoudiniEngineString.ToFString(ValueString);

						// Update the parameter value
						HoudiniParameterFile->SetValueAt(ValueString, Idx);
					}
				}
			}
		}
		break;

		case EHoudiniParameterType::Float:
		{
			UHoudiniParameterFloat* HoudiniParameterFloat = Cast<UHoudiniParameterFloat>(HoudiniParameter);
			if (HoudiniParameterFloat && !HoudiniParameterFloat->IsPendingKill())
			{
				// Set the valueIndex
				HoudiniParameterFloat->SetValueIndex(ParmInfo.floatValuesIndex);
				
				if (bUpdateValue)
				{
					// Update the parameter's value
					HoudiniParameterFloat->SetNumberOfValues(ParmInfo.size);
					if (FHoudiniApi::GetParmFloatValues(
							FHoudiniEngine::Get().GetSession(), InNodeId,
							HoudiniParameterFloat->GetValuesPtr(),
							ParmInfo.floatValuesIndex, ParmInfo.size) != HAPI_RESULT_SUCCESS)
					{
						return false;
					}
				}

				if (bFullUpdate)
				{
					// Only update Unit, no swap, and Min/Max values when doing a full update

					// Get the parameter's unit from the "unit" tag
					FString ParamUnit;
					FHoudiniParameterTranslator::HapiGetParameterUnit(InNodeId, ParmInfo.id, ParamUnit);
					HoudiniParameterFloat->SetUnit(ParamUnit);

					// Get the parameter's no swap tag (hengine_noswap)
					HoudiniParameterFloat->SetNoSwap(HapiGetParameterHasTag(InNodeId, ParmInfo.id, HAPI_PARAM_TAG_NOSWAP));

					// Set the min and max for this parameter
					if (ParmInfo.hasMin)
					{
						HoudiniParameterFloat->SetHasMin(true);
						HoudiniParameterFloat->SetMin(ParmInfo.min);
					}
					else
					{
						HoudiniParameterFloat->SetHasMin(false);
						HoudiniParameterFloat->SetMin(TNumericLimits<float>::Lowest());
					}

					if (ParmInfo.hasMax)
					{
						HoudiniParameterFloat->SetHasMax(true);
						HoudiniParameterFloat->SetMax(ParmInfo.max);
					}
					else
					{
						HoudiniParameterFloat->SetHasMax(false);
						HoudiniParameterFloat->SetMax(TNumericLimits<float>::Max());
					}

					// Set min and max for UI for this property.
					bool bUsesDefaultMin = false;
					if (ParmInfo.hasUIMin)
					{
						HoudiniParameterFloat->SetHasUIMin(true);
						HoudiniParameterFloat->SetUIMin(ParmInfo.UIMin);
					}
					else if (ParmInfo.hasMin)
					{
						// If it is not set, use supplied min.
						HoudiniParameterFloat->SetUIMin(ParmInfo.min);
					}
					else
					{
						// Min value Houdini uses by default.
						HoudiniParameterFloat->SetUIMin(HAPI_UNREAL_PARAM_FLOAT_UI_MIN);
						bUsesDefaultMin = true;
					}

					bool bUsesDefaultMax = false;
					if (ParmInfo.hasUIMax)
					{
						HoudiniParameterFloat->SetHasUIMax(true);
						HoudiniParameterFloat->SetUIMax(ParmInfo.UIMax);
					}
					else if (ParmInfo.hasMax)
					{
						// If it is not set, use supplied max.
						HoudiniParameterFloat->SetUIMax(ParmInfo.max);
					}
					else
					{
						// Max value Houdini uses by default.
						HoudiniParameterFloat->SetUIMax(HAPI_UNREAL_PARAM_FLOAT_UI_MAX);
						bUsesDefaultMax = true;
					}

					if (bUsesDefaultMin || bUsesDefaultMax)
					{
						// If we are using defaults, we can detect some most common parameter names and alter defaults.
						FString LocalParameterName = HoudiniParameterFloat->GetParameterName();
						FHoudiniEngineString HoudiniEngineString(ParmInfo.nameSH);
						HoudiniEngineString.ToFString(LocalParameterName);

						static const FString ParameterNameTranslate(TEXT(HAPI_UNREAL_PARAM_TRANSLATE));
						static const FString ParameterNameRotate(TEXT(HAPI_UNREAL_PARAM_ROTATE));
						static const FString ParameterNameScale(TEXT(HAPI_UNREAL_PARAM_SCALE));
						static const FString ParameterNamePivot(TEXT(HAPI_UNREAL_PARAM_PIVOT));

						if (!LocalParameterName.IsEmpty())
						{
							if (LocalParameterName.Equals(ParameterNameTranslate)
								|| LocalParameterName.Equals(ParameterNameScale)
								|| LocalParameterName.Equals(ParameterNamePivot))
							{
								if (bUsesDefaultMin)
								{
									HoudiniParameterFloat->SetUIMin(-1.0f);
								}
								if (bUsesDefaultMax)
								{
									HoudiniParameterFloat->SetUIMax(1.0f);
								}
							}
							else if (LocalParameterName.Equals(ParameterNameRotate))
							{
								if (bUsesDefaultMin)
								{
									HoudiniParameterFloat->SetUIMin(0.0f);
								}
								if (bUsesDefaultMax)
								{
									HoudiniParameterFloat->SetUIMax(360.0f);
								}
							}
						}
					}
				}
			}	
		}
		break;

		case EHoudiniParameterType::Folder:
		{
			UHoudiniParameterFolder* HoudiniParameterFolder = Cast<UHoudiniParameterFolder>(HoudiniParameter);
			if (HoudiniParameterFolder && !HoudiniParameterFolder->IsPendingKill())
			{
				// Set the valueIndex
				HoudiniParameterFolder->SetValueIndex(ParmInfo.intValuesIndex);
				HoudiniParameterFolder->SetFolderType(GetFolderTypeFromParamInfo(&ParmInfo));
			}
		}
		break;

		case EHoudiniParameterType::FolderList:
		{
			UHoudiniParameterFolderList* HoudiniParameterFolderList = Cast<UHoudiniParameterFolderList>(HoudiniParameter);
			if (HoudiniParameterFolderList && !HoudiniParameterFolderList->IsPendingKill())
			{
				// Set the valueIndex
				HoudiniParameterFolderList->SetValueIndex(ParmInfo.intValuesIndex);
			}
		}
		break;

		case EHoudiniParameterType::Input:
		{
			// Inputs parameters are just stored, and handled separately by UHoudiniInputs
			UHoudiniParameterOperatorPath* HoudiniParameterOperatorPath = Cast<UHoudiniParameterOperatorPath>(HoudiniParameter);
			if (HoudiniParameterOperatorPath && !HoudiniParameterOperatorPath->IsPendingKill())
			{
				/*
				// DO NOT CREATE A DUPLICATE INPUT HERE!
				// Inputs are created by the input translator, and will be tied to this parameter there
				UHoudiniInput * NewInput = NewObject< UHoudiniInput >(
				HoudiniParameterOperatorPath,
				UHoudiniInput::StaticClass());

				UHoudiniAssetComponent *ParentHAC = Cast<UHoudiniAssetComponent>(HoudiniParameterOperatorPath->GetOuter());

				if (!ParentHAC)
					return false;

				if (!NewInput || NewInput->IsPendingKill())
					return false;
				
				// Set the nodeId
				NewInput->SetAssetNodeId(ParentHAC->GetAssetId());
				NewInput->SetInputType(EHoudiniInputType::Geometry);
				HoudiniParameterOperatorPath->HoudiniInputs.Add(NewInput);
				*/
				// Set the valueIndex
				HoudiniParameterOperatorPath->SetValueIndex(ParmInfo.stringValuesIndex);
			}
		}
		break;

		case EHoudiniParameterType::Int:
		{
			UHoudiniParameterInt* HoudiniParameterInt = Cast<UHoudiniParameterInt>(HoudiniParameter);
			if (HoudiniParameterInt && !HoudiniParameterInt->IsPendingKill())
			{
				// Set the valueIndex
				HoudiniParameterInt->SetValueIndex(ParmInfo.intValuesIndex);

				if (bUpdateValue)
				{
					// Get the actual values for this property.
					HoudiniParameterInt->SetNumberOfValues(ParmInfo.size);
					if (FHoudiniApi::GetParmIntValues(
						FHoudiniEngine::Get().GetSession(), InNodeId,
						HoudiniParameterInt->GetValuesPtr(),
						ParmInfo.intValuesIndex, ParmInfo.size) != HAPI_RESULT_SUCCESS)
					{
						return false;
					}
				}

				if (bFullUpdate)
				{
					// Only update unit and Min/Max values for a full update

					// Get the parameter's unit from the "unit" tag
					FString ParamUnit;
					FHoudiniParameterTranslator::HapiGetParameterUnit(InNodeId, ParmInfo.id, ParamUnit);
					HoudiniParameterInt->SetUnit(ParamUnit);

					// Set the min and max for this parameter
					if (ParmInfo.hasMin)
					{
						HoudiniParameterInt->SetHasMin(true);
						HoudiniParameterInt->SetMin((int32)ParmInfo.min);
					}
					else
					{
						HoudiniParameterInt->SetHasMin(false);
						HoudiniParameterInt->SetMin(TNumericLimits<int32>::Lowest());
					}

					if (ParmInfo.hasMax)
					{
						HoudiniParameterInt->SetHasMax(true);
						HoudiniParameterInt->SetMax((int32)ParmInfo.max);
					}
					else
					{
						HoudiniParameterInt->SetHasMax(false);
						HoudiniParameterInt->SetMax(TNumericLimits<int32>::Max());
					}

					// Set min and max for UI for this property.
					if (ParmInfo.hasUIMin)
					{
						HoudiniParameterInt->SetHasUIMin(true);
						HoudiniParameterInt->SetUIMin((int32)ParmInfo.UIMin);
					}
					else if (ParmInfo.hasMin)
					{
						// If it is not set, use supplied min.
						HoudiniParameterInt->SetUIMin((int32)ParmInfo.min);
					}
					else
					{
						// Min value Houdini uses by default.
						HoudiniParameterInt->SetUIMin(HAPI_UNREAL_PARAM_INT_UI_MIN);
					}

					if (ParmInfo.hasUIMax)
					{
						HoudiniParameterInt->SetHasUIMax(true);
						HoudiniParameterInt->SetUIMax((int32)ParmInfo.UIMax);
					}
					else if (ParmInfo.hasMax)
					{
						// If it is not set, use supplied max.
						HoudiniParameterInt->SetUIMax((int32)ParmInfo.max);
					}
					else
					{
						// Max value Houdini uses by default.
						HoudiniParameterInt->SetUIMax(HAPI_UNREAL_PARAM_INT_UI_MAX);
					}
				}
			}
		}
		break;

		case EHoudiniParameterType::IntChoice:
		{
			UHoudiniParameterChoice* HoudiniParameterIntChoice = Cast<UHoudiniParameterChoice>(HoudiniParameter);
			if (HoudiniParameterIntChoice && !HoudiniParameterIntChoice->IsPendingKill())
			{
				// Set the valueIndex
				HoudiniParameterIntChoice->SetValueIndex(ParmInfo.intValuesIndex);

				if (bUpdateValue)
				{
					// Get the actual values for this property.
					int32 CurrentIntValue = 0;
					HOUDINI_CHECK_ERROR_RETURN( FHoudiniApi::GetParmIntValues(
						FHoudiniEngine::Get().GetSession(),
						InNodeId, &CurrentIntValue,
						ParmInfo.intValuesIndex, ParmInfo.size), false);

					// Check the value is valid
					if (CurrentIntValue >= ParmInfo.choiceCount)
					{
						HOUDINI_LOG_WARNING(TEXT("parm '%s' has an invalid value %d, menu tokens are not supported for choice menus"),
							*HoudiniParameterIntChoice->GetParameterName(), CurrentIntValue);
						CurrentIntValue = 0;
					}

					HoudiniParameterIntChoice->SetIntValue(CurrentIntValue);
				}

				// Get the choice descriptors
				if (bFullUpdate)
				{
					// Get the choice descriptors.
					TArray< HAPI_ParmChoiceInfo > ParmChoices;
					ParmChoices.SetNumUninitialized(ParmInfo.choiceCount);
					for (int32 Idx = 0; Idx < ParmChoices.Num(); Idx++)
						FHoudiniApi::ParmChoiceInfo_Init(&(ParmChoices[Idx]));

					HOUDINI_CHECK_ERROR_RETURN( FHoudiniApi::GetParmChoiceLists(
						FHoudiniEngine::Get().GetSession(), 
						InNodeId, &ParmChoices[0],
						ParmInfo.choiceIndex, ParmInfo.choiceCount), false);

					// Set the array sizes
					HoudiniParameterIntChoice->SetNumChoices(ParmInfo.choiceCount);

					bool bMatchedSelectionLabel = false;
					int32 CurrentIntValue = HoudiniParameterIntChoice->GetIntValue();
					for (int32 ChoiceIdx = 0; ChoiceIdx < ParmChoices.Num(); ++ChoiceIdx)
					{
						FString * ChoiceLabel = HoudiniParameterIntChoice->GetStringChoiceLabelAt(ChoiceIdx);
						if (ChoiceLabel)
						{
							FHoudiniEngineString HoudiniEngineString(ParmChoices[ChoiceIdx].labelSH);
							if (!HoudiniEngineString.ToFString(*ChoiceLabel))
								return false;
							//StringChoiceLabels.Add(TSharedPtr< FString >(ChoiceLabel));
						}

						// Match our string value to the corresponding selection label.
						if (ChoiceIdx == CurrentIntValue)
						{
							HoudiniParameterIntChoice->SetStringValue(*ChoiceLabel);
						}
					}
				}
				else if (bUpdateValue)
				{
					// We still need to match the string value to the label
					HoudiniParameterIntChoice->UpdateStringValueFromInt();
				}
			}
		}
		break;

		case EHoudiniParameterType::StringChoice:
		{
			UHoudiniParameterChoice* HoudiniParameterStringChoice = Cast<UHoudiniParameterChoice>(HoudiniParameter);
			if (HoudiniParameterStringChoice && !HoudiniParameterStringChoice->IsPendingKill())
			{
				// Set the valueIndex
				HoudiniParameterStringChoice->SetValueIndex(ParmInfo.stringValuesIndex);

				if (bUpdateValue)
				{
					// Get the actual values for this property.
					HAPI_StringHandle StringHandle;
					HOUDINI_CHECK_ERROR_RETURN( FHoudiniApi::GetParmStringValues(
						FHoudiniEngine::Get().GetSession(),
						InNodeId, false, &StringHandle,
						ParmInfo.stringValuesIndex, ParmInfo.size), false);

					// Get the string value
					FString StringValue;
					FHoudiniEngineString HoudiniEngineString(StringHandle);
					HoudiniEngineString.ToFString(StringValue);

					HoudiniParameterStringChoice->SetStringValue(StringValue);
				}

				// Get the choice descriptors
				if (bFullUpdate)
				{
					// Get the choice descriptors.
					TArray< HAPI_ParmChoiceInfo > ParmChoices;
					ParmChoices.SetNumUninitialized(ParmInfo.choiceCount);
					for (int32 Idx = 0; Idx < ParmChoices.Num(); Idx++)
						FHoudiniApi::ParmChoiceInfo_Init(&(ParmChoices[Idx]));

					HOUDINI_CHECK_ERROR_RETURN( FHoudiniApi::GetParmChoiceLists(
						FHoudiniEngine::Get().GetSession(),
						InNodeId, &ParmChoices[0],
						ParmInfo.choiceIndex, ParmInfo.choiceCount), false);

					// Set the array sizes
					HoudiniParameterStringChoice->SetNumChoices(ParmInfo.choiceCount);

					bool bMatchedSelectionLabel = false;
					FString CurrentStringValue = HoudiniParameterStringChoice->GetStringValue();
					for (int32 ChoiceIdx = 0; ChoiceIdx < ParmChoices.Num(); ++ChoiceIdx)
					{
						FString * ChoiceValue = HoudiniParameterStringChoice->GetStringChoiceValueAt(ChoiceIdx);
						if (ChoiceValue)
						{
							FHoudiniEngineString HoudiniEngineString(ParmChoices[ChoiceIdx].valueSH);
							if (!HoudiniEngineString.ToFString(*ChoiceValue))
								return false;
							//StringChoiceValues.Add(TSharedPtr< FString >(ChoiceValue));
						}

						FString * ChoiceLabel = HoudiniParameterStringChoice->GetStringChoiceLabelAt(ChoiceIdx);
						if (ChoiceLabel)
						{
							FHoudiniEngineString HoudiniEngineString(ParmChoices[ChoiceIdx].labelSH);
							if (!HoudiniEngineString.ToFString(*ChoiceLabel))
								return false;
							//StringChoiceLabels.Add(TSharedPtr< FString >(ChoiceLabel));
						}

						// If this is a string choice list, we need to match name with corresponding selection label.
						if (!bMatchedSelectionLabel && ChoiceValue->Equals(CurrentStringValue))
						{
							bMatchedSelectionLabel = true;
							HoudiniParameterStringChoice->SetIntValue(ChoiceIdx);
						}
					}
				}
				else if (bUpdateValue)
				{
					// We still need to match the string value to the label
					HoudiniParameterStringChoice->UpdateIntValueFromString();
				}
			}
		}
		break;

		case EHoudiniParameterType::Label:
		{
			UHoudiniParameterLabel* HoudiniParameterLabel = Cast<UHoudiniParameterLabel>(HoudiniParameter);
			if (HoudiniParameterLabel && !HoudiniParameterLabel->IsPendingKill())
			{
				if (ParmInfo.type != HAPI_PARMTYPE_LABEL)
					return false;

				// Set the valueIndex
				HoudiniParameterLabel->SetValueIndex(ParmInfo.stringValuesIndex);

				// Get the actual value for this property.
				TArray<HAPI_StringHandle> StringHandles;
				StringHandles.SetNumZeroed(ParmInfo.size);
				FHoudiniApi::GetParmStringValues(
					FHoudiniEngine::Get().GetSession(),
					InNodeId, false, &StringHandles[0],
					ParmInfo.stringValuesIndex, ParmInfo.size);
				
				HoudiniParameterLabel->EmptyLabelString();

				// Convert HAPI string handles to Unreal strings.
				for (int32 Idx = 0; Idx < StringHandles.Num(); ++Idx)
				{
					FString ValueString = TEXT("");
					FHoudiniEngineString HoudiniEngineString(StringHandles[Idx]);
					HoudiniEngineString.ToFString(ValueString);
					HoudiniParameterLabel->AddLabelString(ValueString);
				}
			}
		}
		break;

		case EHoudiniParameterType::MultiParm:
		{
			UHoudiniParameterMultiParm* HoudiniParameterMulti = Cast<UHoudiniParameterMultiParm>(HoudiniParameter);
			if (HoudiniParameterMulti && !HoudiniParameterMulti->IsPendingKill())
			{
				if (ParmInfo.type != HAPI_PARMTYPE_MULTIPARMLIST)
					return false;

				// Set the valueIndex
				HoudiniParameterMulti->SetValueIndex(ParmInfo.intValuesIndex);

				// Set the multiparm value
				int32 MultiParmValue = 0;
				HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::GetParmIntValues(
					FHoudiniEngine::Get().GetSession(),
					InNodeId, &MultiParmValue, ParmInfo.intValuesIndex, 1), false);

				HoudiniParameterMulti->SetValue(MultiParmValue);
				HoudiniParameterMulti->MultiParmInstanceCount = ParmInfo.instanceCount;
				HoudiniParameterMulti->MultiParmInstanceLength = ParmInfo.instanceLength;

			}
		}
		break;

		case EHoudiniParameterType::Separator:
		{
			UHoudiniParameterSeparator* HoudiniParameterSeparator = Cast<UHoudiniParameterSeparator>(HoudiniParameter);
			if (HoudiniParameterSeparator && !HoudiniParameterSeparator->IsPendingKill())
			{
				// We can only handle separator type.
				if (ParmInfo.type != HAPI_PARMTYPE_SEPARATOR)
					return false;

				// Set the valueIndex
				HoudiniParameterSeparator->SetValueIndex(ParmInfo.stringValuesIndex);
			}
		}
		break;

		case EHoudiniParameterType::String:
		case EHoudiniParameterType::StringAssetRef:
		{
			UHoudiniParameterString* HoudiniParameterString = Cast<UHoudiniParameterString>(HoudiniParameter);
			if (HoudiniParameterString && !HoudiniParameterString->IsPendingKill())
			{
				// We can only handle string type.
				if (ParmInfo.type != HAPI_PARMTYPE_STRING && ParmInfo.type != HAPI_PARMTYPE_NODE)
					return false;

				// Set the valueIndex
				HoudiniParameterString->SetValueIndex(ParmInfo.stringValuesIndex);

				// Stop if we don't want to update the value
				if (bUpdateValue)
				{
					// Get the actual value for this property.
					TArray< HAPI_StringHandle > StringHandles;
					StringHandles.SetNumZeroed(ParmInfo.size);
					if (FHoudiniApi::GetParmStringValues(
						FHoudiniEngine::Get().GetSession(),
						InNodeId, false, &StringHandles[0],
						ParmInfo.stringValuesIndex, ParmInfo.size) != HAPI_RESULT_SUCCESS)
					{
						return false;
					}

					// Convert HAPI string handles to Unreal strings.
					HoudiniParameterString->SetNumberOfValues(ParmInfo.size);
					for (int32 Idx = 0; Idx < StringHandles.Num(); ++Idx)
					{
						FString ValueString = TEXT("");
						FHoudiniEngineString HoudiniEngineString(StringHandles[Idx]);
						HoudiniEngineString.ToFString(ValueString);
						HoudiniParameterString->SetValueAt(ValueString, Idx);
					}
				}

				if (bFullUpdate)
				{
					// Check if the parameter has the "asset_ref" tag
					HoudiniParameterString->SetIsAssetRef(
						FHoudiniParameterTranslator::HapiGetParameterHasTag(InNodeId, ParmInfo.id, HAPI_PARAM_TAG_ASSET_REF));
				}
			}
		}
		break;

		case EHoudiniParameterType::Toggle:
		{
			UHoudiniParameterToggle* HoudiniParameterToggle = Cast<UHoudiniParameterToggle>(HoudiniParameter);
			if (HoudiniParameterToggle && !HoudiniParameterToggle->IsPendingKill())
			{
				if (ParmInfo.type != HAPI_PARMTYPE_TOGGLE)
					return false;

				// Set the valueIndex
				HoudiniParameterToggle->SetValueIndex(ParmInfo.intValuesIndex);

				// Stop if we don't want to update the value
				if (bUpdateValue)
				{
					// Get the actual values for this property.
					HoudiniParameterToggle->SetNumberOfValues(ParmInfo.size);
					if (FHoudiniApi::GetParmIntValues(
						FHoudiniEngine::Get().GetSession(), InNodeId,
						HoudiniParameterToggle->GetValuesPtr(),
						ParmInfo.intValuesIndex, ParmInfo.size) != HAPI_RESULT_SUCCESS)
					{
						return false;
					}
				}
			}
		}
		break;

		case EHoudiniParameterType::Invalid:
		{
			// TODO
		}
		break;
	}

	return true;
}

bool
FHoudiniParameterTranslator::HapiGetParameterTagValue(const HAPI_NodeId& NodeId, const HAPI_ParmId& ParmId, const FString& Tag, FString& TagValue)
{
	// Default
	TagValue = FString();

	// Does the parameter has the tag?
	bool HasTag = false;
	HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::ParmHasTag(
		FHoudiniEngine::Get().GetSession(), NodeId, ParmId,
		TCHAR_TO_ANSI(*Tag), &HasTag), false);

	if (!HasTag)
		return false;

	// Get the tag string value
	HAPI_StringHandle StringHandle;
	HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::GetParmTagValue(
		FHoudiniEngine::Get().GetSession(), NodeId, ParmId,
		TCHAR_TO_ANSI(*Tag), &StringHandle), false);

	FHoudiniEngineString HoudiniEngineString(StringHandle);
	if (HoudiniEngineString.ToFString(TagValue))
	{
		return true;
	}

	return false;
}


bool
FHoudiniParameterTranslator::HapiGetParameterUnit(const HAPI_NodeId& NodeId, const HAPI_ParmId& ParmId, FString& OutUnitString)
{
	//
	OutUnitString = TEXT("");

	// We're looking for the parameter unit tag
	//FString UnitTag = "units";	

	// Get the actual string value.
	FString UnitString = TEXT("");
	if (!FHoudiniParameterTranslator::HapiGetParameterTagValue(NodeId, ParmId, "units", UnitString))
		return false;
	
	// We need to do some replacement in the string here in order to be able to get the
	// proper unit type when calling FUnitConversion::UnitFromString(...) after.

	// Per second and per hour are the only "per" unit that unreal recognize
	UnitString.ReplaceInline(TEXT("s-1"), TEXT("/s"));
	UnitString.ReplaceInline(TEXT("h-1"), TEXT("/h"));

	// Houdini likes to add '1' on all the unit, so we'll remove all of them
	// except the '-1' that still needs to remain.
	UnitString.ReplaceInline(TEXT("-1"), TEXT("--"));
	UnitString.ReplaceInline(TEXT("1"), TEXT(""));
	UnitString.ReplaceInline(TEXT("--"), TEXT("-1"));

	OutUnitString = UnitString;

	return true;
}

bool
FHoudiniParameterTranslator::HapiGetParameterHasTag(const HAPI_NodeId& NodeId, const HAPI_ParmId& ParmId, const FString& Tag)
{
	// Does the parameter has the tag we're looking for?
	bool HasTag = false;
	HOUDINI_CHECK_ERROR_RETURN( FHoudiniApi::ParmHasTag(
		FHoudiniEngine::Get().GetSession(), NodeId, ParmId,
		TCHAR_TO_ANSI(*Tag), &HasTag), false);

	return HasTag;
}


bool
FHoudiniParameterTranslator::UploadChangedParameters( UHoudiniAssetComponent * HAC )
{
	if (!HAC || HAC->IsPendingKill())
		return false;

	for (int32 ParmIdx = 0; ParmIdx < HAC->GetNumParameters(); ParmIdx++)
	{
		UHoudiniParameter*& CurrentParm = HAC->Parameters[ParmIdx];
		if (!CurrentParm || CurrentParm->IsPendingKill() || !CurrentParm->HasChanged())
			continue;

		bool bSuccess = false;
		if (CurrentParm->IsPendingRevertToDefault())
		{
			bSuccess = RevertParameterToDefault(CurrentParm);
		}
		else
		{
			bSuccess = UploadParameterValue(CurrentParm);
		}


		if (bSuccess)
		{
			CurrentParm->MarkChanged(false);
			//CurrentParm->SetNeedsToTriggerUpdate(false);
		}
		else
		{
			// Keep this param marked as changed but prevent it from generating updates
			CurrentParm->SetNeedsToTriggerUpdate(false);
		}
	}

	return true;
}

bool
FHoudiniParameterTranslator::UploadParameterValue(UHoudiniParameter* InParam)
{
	if (!InParam || InParam->IsPendingKill())
		return false;

	switch (InParam->GetParameterType())
	{
		case EHoudiniParameterType::Float:
		{
			UHoudiniParameterFloat* FloatParam = Cast<UHoudiniParameterFloat>(InParam);
			if (!FloatParam || FloatParam->IsPendingKill())
			{
				return false;
			}

			float* DataPtr = FloatParam->GetValuesPtr();
			if (!DataPtr)
			{
				return false;
			}

			HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::SetParmFloatValues(
				FHoudiniEngine::Get().GetSession(),
				FloatParam->GetNodeId(), DataPtr, FloatParam->GetValueIndex(), FloatParam->GetTupleSize()), false);
		}
		break;

		case EHoudiniParameterType::Int:
		{
			UHoudiniParameterInt* IntParam = Cast<UHoudiniParameterInt>(InParam);
			if (!IntParam || IntParam->IsPendingKill())
			{
				return false;
			}

			int32* DataPtr = IntParam->GetValuesPtr();
			if (!DataPtr)
			{
				return false;
			}

			HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::SetParmIntValues(
				FHoudiniEngine::Get().GetSession(),
				IntParam->GetNodeId(), DataPtr, IntParam->GetValueIndex(), IntParam->GetTupleSize()), false);
		}
		break;

		case EHoudiniParameterType::String:
		{
			UHoudiniParameterString* StringParam = Cast<UHoudiniParameterString>(InParam);
			if (!StringParam || StringParam->IsPendingKill())
			{
				return false;
			}

			int32 NumValues = StringParam->GetNumberOfValues();
			if (NumValues <= 0)
			{
				return false;
			}

			for (int32 Idx = 0; Idx < NumValues; Idx++)
			{
				std::string ConvertedString = TCHAR_TO_UTF8(*(StringParam->GetValueAt(Idx)));
				HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::SetParmStringValue(
					FHoudiniEngine::Get().GetSession(),
					StringParam->GetNodeId(), ConvertedString.c_str(), StringParam->GetParmId(), Idx), false);
			}
		}
		break;

		case EHoudiniParameterType::IntChoice:
		{
			UHoudiniParameterChoice* ChoiceParam = Cast<UHoudiniParameterChoice>(InParam);
			if (!ChoiceParam || ChoiceParam->IsPendingKill())
				return false;

			// Set the parameter's int value.
			int32 IntValue = ChoiceParam->GetIntValue();
			HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::SetParmIntValues(
				FHoudiniEngine::Get().GetSession(),
				ChoiceParam->GetNodeId(), &IntValue, ChoiceParam->GetValueIndex(), ChoiceParam->GetTupleSize()), false);
		}
		break;
		case EHoudiniParameterType::StringChoice:
		{
			UHoudiniParameterChoice* ChoiceParam = Cast<UHoudiniParameterChoice>(InParam);
			if (!ChoiceParam || ChoiceParam->IsPendingKill())
			{
				return false;
			}

			if (ChoiceParam->IsStringChoice())
			{
				// Set the parameter's string value.
				std::string ConvertedString = TCHAR_TO_UTF8(*(ChoiceParam->GetStringValue()));
				HOUDINI_CHECK_ERROR_RETURN( FHoudiniApi::SetParmStringValue(
					FHoudiniEngine::Get().GetSession(),
					ChoiceParam->GetNodeId(), ConvertedString.c_str(), ChoiceParam->GetParmId(), 0), false);
			}
			else
			{
				// Set the parameter's int value.
				int32 IntValue = ChoiceParam->GetIntValue();
				HOUDINI_CHECK_ERROR_RETURN( FHoudiniApi::SetParmIntValues(
					FHoudiniEngine::Get().GetSession(),
					ChoiceParam->GetNodeId(), &IntValue, ChoiceParam->GetValueIndex(), ChoiceParam->GetTupleSize()), false);
			}
		}
		break;

		case EHoudiniParameterType::Color:
		{	
			UHoudiniParameterColor* ColorParam = Cast<UHoudiniParameterColor>(InParam);
			if (!ColorParam || ColorParam->IsPendingKill())
				return false;

			FLinearColor Color = ColorParam->GetColorValue();
			
			// Set the color value
			HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::SetParmFloatValues(
				FHoudiniEngine::Get().GetSession(),
				ColorParam->GetNodeId(),
				(float*)(&Color.R), ColorParam->GetValueIndex(), 3), false);
		
		}
		break;

		case EHoudiniParameterType::Button:
		{
			UHoudiniParameterButton* ButtonParam = Cast<UHoudiniParameterButton>(InParam);
			if (!ButtonParam)
				return false;

			TArray<int32> DataArray;
			DataArray.Add(1);

			// Set the button parameter value to 1, (setting button param to any value will call the callback function.)
			HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::SetParmIntValues(
				FHoudiniEngine::Get().GetSession(),
				ButtonParam->GetNodeId(),
				DataArray.GetData(),
				ButtonParam->GetValueIndex(), 1), false);
		}
		break;

		case EHoudiniParameterType::Toggle: 
		{
			UHoudiniParameterToggle* ToggleParam = Cast<UHoudiniParameterToggle>(InParam);
			if (!ToggleParam)
				return false;

			// Set the toggle parameter values.
			HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::SetParmIntValues(
				FHoudiniEngine::Get().GetSession(),
				ToggleParam->GetNodeId(),
				ToggleParam->GetValuesPtr(),
				ToggleParam->GetValueIndex(), ToggleParam->GetTupleSize()), false);
		}
		break;

		case EHoudiniParameterType::File:
		case EHoudiniParameterType::FileDir:
		case EHoudiniParameterType::FileGeo:
		case EHoudiniParameterType::FileImage:
		{
			UHoudiniParameterFile* FileParam = Cast<UHoudiniParameterFile>(InParam);

			if (!UploadDirectoryPath(FileParam))
				return false;
		}
		break;

		case EHoudiniParameterType::MultiParm: 
		{
			if (!UploadMultiParmValues(InParam))
				return false;
		}

		break;

		case EHoudiniParameterType::FloatRamp:
		{
			if (!UploadRampParameter(InParam))
				return false;
		}
		break;

		case EHoudiniParameterType::ColorRamp:
		{
			if (!UploadRampParameter(InParam))
				return false;
		}
		break;

		default:
		{
			// TODO: implement other parameter types!
			return false;
		}
		break;
	}

	// The parameter is no longer considered as changed
	InParam->MarkChanged(false);

	return true;
}

bool
FHoudiniParameterTranslator::RevertParameterToDefault(UHoudiniParameter* InParam)
{
	if (!InParam || InParam->IsPendingKill())
		return false;

	if (!InParam->IsPendingRevertToDefault())
		return false;

	TArray<int32> TupleToRevert;
	InParam->GetTuplePendingRevertToDefault(TupleToRevert);
	if (TupleToRevert.Num() <= 0)
		return false;

	FString ParameterName = InParam->GetParameterName();

	bool bReverted = true;
	for (auto CurrentIdx : TupleToRevert )
	{
		if (!TupleToRevert.IsValidIndex(CurrentIdx))
		{
			// revert the whole parameter to its default value
			if (HAPI_RESULT_SUCCESS != FHoudiniApi::RevertParmToDefaults(
				FHoudiniEngine::Get().GetSession(),
				InParam->GetNodeId(), TCHAR_TO_UTF8(*ParameterName)))
			{
				HOUDINI_LOG_WARNING(TEXT("Failed to revert parameter %s to its default value."), *ParameterName);
				bReverted = false;
			}
		}
		else
		{
			// revert a tuple to its default value
			if (HAPI_RESULT_SUCCESS != FHoudiniApi::RevertParmToDefault(
				FHoudiniEngine::Get().GetSession(),
				InParam->GetNodeId(), TCHAR_TO_UTF8(*ParameterName), CurrentIdx))
			{
				HOUDINI_LOG_WARNING(TEXT("Failed to revert parameter %s - %d to its default value."), *ParameterName, CurrentIdx);
				bReverted = false;
			}
		}
	}

	if (!bReverted)
		return false;

	// The parameter no longer needs to be reverted
	InParam->MarkDefault(true);

	return true;
}

EHoudiniFolderParameterType 
FHoudiniParameterTranslator::GetFolderTypeFromParamInfo(const HAPI_ParmInfo* ParamInfo) 
{
	EHoudiniFolderParameterType Type = EHoudiniFolderParameterType::Invalid;

	switch (ParamInfo->scriptType) 
	{
	case HAPI_PrmScriptType::HAPI_PRM_SCRIPT_TYPE_GROUPSIMPLE:
		Type = EHoudiniFolderParameterType::Simple;
		break;

	case HAPI_PrmScriptType::HAPI_PRM_SCRIPT_TYPE_GROUPCOLLAPSIBLE:
		Type = EHoudiniFolderParameterType::Collapsible;
		break;

	case HAPI_PrmScriptType::HAPI_PRM_SCRIPT_TYPE_GROUP:
		Type = EHoudiniFolderParameterType::Tabs;
		break;

	case HAPI_PrmScriptType::HAPI_PRM_SCRIPT_TYPE_GROUPRADIO:
		Type = EHoudiniFolderParameterType::Radio;
		break;

	default:
		Type = EHoudiniFolderParameterType::Other;
		break;
	
	}
	
	return Type;

}

bool
FHoudiniParameterTranslator::SyncMultiParmValuesAtLoad(UHoudiniParameter* InParam, TArray<UHoudiniParameter*> &OldParams, const int32& InAssetId, const int32 CurrentIndex)
{

	UHoudiniParameterMultiParm* MultiParam = Cast<UHoudiniParameterMultiParm>(InParam);

	if (!MultiParam || MultiParam->IsPendingKill())
		return false;

	UHoudiniParameterRampFloat* FloatRampParameter = nullptr;
	UHoudiniParameterRampColor* ColorRampParameter = nullptr;

	if (MultiParam->GetParameterType() == EHoudiniParameterType::FloatRamp)
		FloatRampParameter = Cast<UHoudiniParameterRampFloat>(MultiParam);

	else if (MultiParam->GetParameterType() == EHoudiniParameterType::ColorRamp)
		ColorRampParameter = Cast<UHoudiniParameterRampColor>(MultiParam);

	// Get the asset's info
	HAPI_AssetInfo AssetInfo;
	HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::GetAssetInfo(
		FHoudiniEngine::Get().GetSession(), InAssetId, &AssetInfo), false);

	HAPI_NodeId NodeId = AssetInfo.nodeId;

	int32 Idx = 0;
	int32 InstanceCount = -1;
	HAPI_ParmId ParmId = -1;
	TArray<HAPI_ParmInfo> ParmInfos;
	if (!GetMultiParmInstanceStartIdx(AssetInfo, MultiParam->GetParameterName(), Idx, InstanceCount, ParmId, ParmInfos))
		return false;

	
	for (int n = 0; n < InstanceCount - MultiParam->GetInstanceCount(); ++n) 
	{
		FHoudiniApi::RemoveMultiparmInstance(
			FHoudiniEngine::Get().GetSession(), NodeId,
			ParmId, MultiParam->InstanceStartOffset);
	}

	for (int n = 0; n < MultiParam->GetInstanceCount() - InstanceCount; ++n) 
	{
		FHoudiniApi::InsertMultiparmInstance(
			FHoudiniEngine::Get().GetSession(), NodeId,
			ParmId, MultiParam->InstanceStartOffset);
	}

	
	// Sync nested multi-params recursively
	for (int32 ParamIdx = CurrentIndex; ParamIdx < OldParams.Num(); ++ParamIdx) 
	{
		UHoudiniParameter* NextParm = OldParams[ParamIdx];
		if (NextParm->GetParentParmId() == ParmId) 
		{
			if (NextParm->GetParameterType() == EHoudiniParameterType::MultiParm) 
			{
				SyncMultiParmValuesAtLoad(NextParm, OldParams, InAssetId, ParamIdx);
			}
		}
	}


	// The multiparm is a ramp, Get the param infos again, since the number of param instances is changed
	if (!GetMultiParmInstanceStartIdx(AssetInfo, InParam->GetParameterName(), Idx, InstanceCount, ParmId, ParmInfos))
		return false;

	// Step 3:  Set values of the inserted points
	if (FloatRampParameter)
	{
		for (auto & Point : FloatRampParameter->Points)
		{
			// 1: update position float at param Idx
			FHoudiniApi::SetParmFloatValues(
				FHoudiniEngine::Get().GetSession(),
				NodeId, &(Point->Position), ParmInfos[Idx].floatValuesIndex, 1);

			// 2: update float value at param Idx + 1
			// float value
			FHoudiniApi::SetParmFloatValues(
				FHoudiniEngine::Get().GetSession(),
				NodeId, &(Point->Value), ParmInfos[Idx + 1].floatValuesIndex, 1);

			// 3: update interpolation type at param Idx + 2
			int32 IntValue = (int32)(Point->Interpolation);
			FHoudiniApi::SetParmIntValues(
				FHoudiniEngine::Get().GetSession(),
				NodeId, &IntValue, ParmInfos[Idx + 2].intValuesIndex, 1);

			Idx += 3;
		}
	}
	else if (ColorRampParameter)
	{
		for (auto& Point : ColorRampParameter->Points)
		{
			// 1: update position float at param Idx
			FHoudiniApi::SetParmFloatValues(
				FHoudiniEngine::Get().GetSession(),
				NodeId, &(Point->Position), ParmInfos[Idx].floatValuesIndex, 1);

			// 2: update color value at param Idx + 1
			// float value
			FHoudiniApi::SetParmFloatValues(
				FHoudiniEngine::Get().GetSession(),
				NodeId, (float*)(&Point->Value.R), ParmInfos[Idx + 1].floatValuesIndex, 3);

			// 3: update interpolation type at param Idx + 2
			int32 IntValue = (int32)(Point->Interpolation);
			FHoudiniApi::SetParmIntValues(
				FHoudiniEngine::Get().GetSession(),
				NodeId, &IntValue, ParmInfos[Idx + 2].intValuesIndex, 1);

			Idx += 3;
		}
	}


	return true;
}


bool FHoudiniParameterTranslator::UploadRampParameter(UHoudiniParameter* InParam) 
{
	UHoudiniParameterMultiParm* MultiParam = Cast<UHoudiniParameterMultiParm>(InParam);
	if (!MultiParam || MultiParam->IsPendingKill())
		return false;

	UHoudiniAssetComponent* HoudiniAssetComponent = Cast<UHoudiniAssetComponent>(InParam->GetOuter());
	if (!HoudiniAssetComponent)
		return false;

	int32 InsertIndexStart = -1;
	UHoudiniParameterRampFloat* RampFloatParam = Cast<UHoudiniParameterRampFloat>(InParam);
	UHoudiniParameterRampColor* RampColorParam = Cast<UHoudiniParameterRampColor>(InParam);

	TArray<UHoudiniParameterRampModificationEvent*> *Events = nullptr;
	if (RampFloatParam)
	{
		Events = &(RampFloatParam->ModificationEvents);
		InsertIndexStart = RampFloatParam->GetInstanceCount();
	}
	else if (RampColorParam)
	{
		Events = &(RampColorParam->ModificationEvents);
		InsertIndexStart = RampColorParam->GetInstanceCount();
	}
	else
		return false;

	// Handle All Events
	Events->Sort([](const UHoudiniParameterRampModificationEvent& A, const UHoudiniParameterRampModificationEvent& B) 
	{
		return A.DeleteInstanceIndex > B.DeleteInstanceIndex;
	});
	

	// Step 1:  Handle all delete events first
	for (auto& Event : *Events) 
	{
		if (!Event)
			continue;

		if (!Event->IsDeleteEvent())
			continue;
		
		FHoudiniApi::RemoveMultiparmInstance(
			FHoudiniEngine::Get().GetSession(), MultiParam->GetNodeId(),
			MultiParam->GetParmId(), Event->DeleteInstanceIndex + MultiParam->InstanceStartOffset);

		InsertIndexStart -= 1;
	}

	int32 InsertIndex = InsertIndexStart;


	// Step 2:  Handle all insert events
	for (auto& Event : *Events) 
	{
		if (!Event)
			continue;

		if (!Event->IsInsertEvent())
			continue;
		
		FHoudiniApi::InsertMultiparmInstance(
			FHoudiniEngine::Get().GetSession(), MultiParam->GetNodeId(),
			MultiParam->GetParmId(), InsertIndex + MultiParam->InstanceStartOffset);

		InsertIndex += 1;
	}
	
	// Step 3:  Set inserted parameter values (only if there are instances inserted)
	if (InsertIndex > InsertIndexStart)
	{
		if (HoudiniAssetComponent) 
		{
			// Get the asset's info
			HAPI_AssetInfo AssetInfo;
			HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::GetAssetInfo(
				FHoudiniEngine::Get().GetSession(), HoudiniAssetComponent->AssetId, &AssetInfo), false);

			int32 Idx = 0;
			int32 InstanceCount = -1;
			HAPI_ParmId ParmId = -1;
			TArray< HAPI_ParmInfo > ParmInfos;

			if (!FHoudiniParameterTranslator::GetMultiParmInstanceStartIdx(AssetInfo, InParam->GetParameterName(),
				Idx, InstanceCount, ParmId, ParmInfos))
				return false;

			if (InstanceCount < 0)
				return false;

			// Instance count doesn't match, 
			if (InsertIndex != InstanceCount)
				return false;


			// Starting index of parameters which just inserted
			Idx += 3 * InsertIndexStart;
			

			for (auto & Event : *Events)
			{
				if (!Event)
					continue;
				
				if (!Event->IsInsertEvent())
					continue;

				// 1: update position float at param Idx
				FHoudiniApi::SetParmFloatValues(
					FHoudiniEngine::Get().GetSession(),
					AssetInfo.nodeId, &(Event->InsertPosition), ParmInfos[Idx].floatValuesIndex, 1);

				// step 2: update value at param Idx + 1
				if (Event->IsFloatRampEvent())
				{
					// float value
					FHoudiniApi::SetParmFloatValues(
						FHoudiniEngine::Get().GetSession(),
						AssetInfo.nodeId, &(Event->InsertFloat), ParmInfos[Idx + 1].floatValuesIndex, 1);
				}
				else
				{
					// color value
					FHoudiniApi::SetParmFloatValues(
						FHoudiniEngine::Get().GetSession(),
						AssetInfo.nodeId, (float*)(&Event->InsertColor.R), ParmInfos[Idx + 1].floatValuesIndex, 3);
				}

				// step 3: update interpolation type at param Idx + 2
				int32 IntValue = (int32)(Event->InsertInterpolation);
				FHoudiniApi::SetParmIntValues(
					FHoudiniEngine::Get().GetSession(),
					AssetInfo.nodeId, &IntValue, ParmInfos[Idx + 2].intValuesIndex, 1);
				
				Idx += 3;
			}
		}
	}

	// Step 4: clear all events
	Events->Empty();

	return true;
}

bool FHoudiniParameterTranslator::UploadMultiParmValues(UHoudiniParameter* InParam) 
{
	UHoudiniParameterMultiParm* MultiParam = Cast<UHoudiniParameterMultiParm>(InParam);
	if (!MultiParam)
		return false;

	TArray<EHoudiniMultiParmModificationType> &LastModificationArray = MultiParam->MultiParmInstanceLastModifyArray;

	int32 Size = MultiParam->MultiParmInstanceLastModifyArray.Num();

	for (int32 Index = 0; Index < Size; ++Index)
	{
		if (LastModificationArray[Index] == EHoudiniMultiParmModificationType::Inserted)
		{
			if (HAPI_RESULT_SUCCESS != FHoudiniApi::InsertMultiparmInstance(
				FHoudiniEngine::Get().GetSession(), MultiParam->GetNodeId(),
				MultiParam->GetParmId(), Index + MultiParam->InstanceStartOffset))
				return false;	
			
		}
	}

	for (int32 Index = Size - 1; Index >= 0; --Index)
	{
		if (LastModificationArray[Index] == EHoudiniMultiParmModificationType::Removed)
		{
			if (HAPI_RESULT_SUCCESS != FHoudiniApi::RemoveMultiparmInstance(
				FHoudiniEngine::Get().GetSession(), MultiParam->GetNodeId(),
				MultiParam->GetParmId(), Index + MultiParam->InstanceStartOffset))
				return false;

			Size -= 1;
		}
	}

	// Remove all removal events.
	for (int32 Index = Size - 1; Index >= 0; --Index) 
	{
		if (LastModificationArray[Index] == EHoudiniMultiParmModificationType::Removed)
			LastModificationArray.RemoveAt(Index);
	}
	
	// The last modification array is resized.
	Size = LastModificationArray.Num();

	// Reset the last modification array
	for (int32 Itr =Size - 1; Itr >= 0; --Itr)
	{
		LastModificationArray[Itr] = EHoudiniMultiParmModificationType::None;
	}

	MultiParam->MultiParmInstanceCount = Size;

	return true;
}

bool
FHoudiniParameterTranslator::UploadDirectoryPath(UHoudiniParameterFile* InParam) 
{
	if(!InParam)
		return false;

	for (int32 Index = 0; Index < InParam->GetNumValues(); ++Index)
	{
		std::string ConvertedString = TCHAR_TO_UTF8(*(InParam->GetValueAt(Index)));
		HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::SetParmStringValue(FHoudiniEngine::Get().GetSession(),
			InParam->GetNodeId(), ConvertedString.c_str(), InParam->GetParmId(), Index), false);
	}

	return true;
}

bool
FHoudiniParameterTranslator::GetMultiParmInstanceStartIdx(HAPI_AssetInfo& InAssetInfo, const FString InParmName,
	int32& OutStartIdx, int32& OutInstanceCount, HAPI_ParmId& OutParmId, TArray<HAPI_ParmInfo> &OutParmInfos)
{
	// Reset outputs
	OutStartIdx = 0;
	OutInstanceCount = -1;
	OutParmId = -1;
	OutParmInfos.Empty();

	// .. the asset's node info
	HAPI_NodeInfo NodeInfo;
	HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::GetNodeInfo(
		FHoudiniEngine::Get().GetSession(), InAssetInfo.nodeId, &NodeInfo), false);

	OutParmInfos.SetNumUninitialized(NodeInfo.parmCount);
	HOUDINI_CHECK_ERROR_RETURN(FHoudiniApi::GetParameters(
		FHoudiniEngine::Get().GetSession(), InAssetInfo.nodeId, &OutParmInfos[0], 0, NodeInfo.parmCount), false);


	while (OutStartIdx < OutParmInfos.Num())
	{
		FString ParmNameBuffer;
		FHoudiniEngineString(OutParmInfos[OutStartIdx].nameSH).ToFString(ParmNameBuffer);

		if (ParmNameBuffer == InParmName)
		{
			OutParmId = OutParmInfos[OutStartIdx].id;
			OutInstanceCount = OutParmInfos[OutStartIdx].instanceCount;
			break;
		}

		OutStartIdx += 1;
	}

	// Start index of the ramp children parameters
	OutStartIdx += 1;

	return true;
}