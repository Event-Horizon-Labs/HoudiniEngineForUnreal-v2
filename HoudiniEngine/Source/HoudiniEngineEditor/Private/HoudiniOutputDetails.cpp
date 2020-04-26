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

#include "HoudiniOutputDetails.h"

#include "HoudiniAssetComponentDetails.h"
#include "HoudiniMeshTranslator.h"
#include "HoudiniInstanceTranslator.h"
#include "HoudiniAssetComponent.h"
#include "HoudiniEngine.h"
#include "HoudiniEngineUtils.h"
#include "HoudiniEngineBakeUtils.h"
#include "HoudiniEngineEditor.h"
#include "HoudiniEngineEditorUtils.h"
#include "HoudiniEnginePrivatePCH.h"
#include "HoudiniEngineEditorPrivatePCH.h"
#include "HoudiniEngineRuntimePrivatePCH.h"
#include "HoudiniAsset.h"
#include "HoudiniSplineComponent.h"
#include "HoudiniStaticMesh.h"
#include "HoudiniEngineCommands.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Input/SRotatorInputBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Editor/UnrealEd/Public/AssetThumbnail.h"
#include "SAssetDropTarget.h"
#include "Engine/StaticMesh.h"
#include "Components/SplineComponent.h"
#include "Materials/Material.h"
#include "Sound/SoundBase.h"
#include "Engine/SkeletalMesh.h"
#include "Particles/ParticleSystem.h"
//#include "Landscape.h"
#include "LandscapeProxy.h"
#include "ScopedTransaction.h"
#include "PhysicsEngine/BodySetup.h"

#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"

#include "Editor/PropertyEditor/Public/PropertyCustomizationHelpers.h"

#define LOCTEXT_NAMESPACE HOUDINI_LOCTEXT_NAMESPACE

void
FHoudiniOutputDetails::CreateWidget(
	IDetailCategoryBuilder& HouOutputCategory,
	TArray<UHoudiniOutput*> InOutputs)
{
	if (InOutputs.Num() <= 0)
		return;

	UHoudiniOutput* MainOutput = InOutputs[0];

	// Don't create UI for editable curve.
	if (!MainOutput || MainOutput->IsPendingKill() || MainOutput->IsEditableNode()) 
		return;

	// Get thumbnail pool for this builder.
	TSharedPtr< FAssetThumbnailPool > AssetThumbnailPool = HouOutputCategory.GetParentLayout().GetThumbnailPool();

	// TODO
	// For now we just handle Mesh Outputs

	switch (MainOutput->GetType()) 
	{
		case EHoudiniOutputType::Mesh: 
		{
			FHoudiniOutputDetails::CreateMeshOutputWidget(HouOutputCategory, MainOutput);
			break;
		}

		case EHoudiniOutputType::Landscape: 
		{
			FHoudiniOutputDetails::CreateLandscapeOutputWidget(HouOutputCategory, MainOutput);
			break;
		}

		case EHoudiniOutputType::Instancer:
		{
			FHoudiniOutputDetails::CreateInstancerOutputWidget(HouOutputCategory, MainOutput);
			break;
		}

		case EHoudiniOutputType::Curve:
		{
			FHoudiniOutputDetails::CreateCurveOutputWidget(HouOutputCategory, MainOutput);
			break;
		}
		case EHoudiniOutputType::Skeletal:
		default: 
		{
			FHoudiniOutputDetails::CreateDefaultOutputWidget(HouOutputCategory, MainOutput);
			break;
		}

	}

}


void 
FHoudiniOutputDetails::CreateLandscapeOutputWidget(
	IDetailCategoryBuilder& HouOutputCategory,
	UHoudiniOutput* InOutput)
{
	if (!InOutput || InOutput->IsPendingKill())
		return;

	// Go through this output's objects
	TMap<FHoudiniOutputObjectIdentifier, FHoudiniOutputObject>& OutputObjects = InOutput->GetOutputObjects();
	for (auto& CurrentOutputObj : OutputObjects) 
	{
		UHoudiniLandscapePtr* LandscapePointer = Cast<UHoudiniLandscapePtr>(CurrentOutputObj.Value.OutputObject);
		if (!LandscapePointer)
			continue;

		FHoudiniOutputObjectIdentifier& Identifier = CurrentOutputObj.Key;
		const FHoudiniGeoPartObject *HGPO = nullptr;
		for (const auto& CurHGPO : InOutput->GetHoudiniGeoPartObjects())
		{
			if (!Identifier.Matches(CurHGPO))
				continue;

			HGPO = &CurHGPO;
			break;
		}

		if (!HGPO)
			continue;

		CreateLandscapeOutputWidget_Helper(HouOutputCategory, InOutput, *HGPO, LandscapePointer, Identifier);
	}
}

void
FHoudiniOutputDetails::CreateLandscapeOutputWidget_Helper(
	IDetailCategoryBuilder& HouOutputCategory,
	UHoudiniOutput* InOutput,
	const FHoudiniGeoPartObject& HGPO,
	UHoudiniLandscapePtr* LandscapePointer,
	const FHoudiniOutputObjectIdentifier & OutputIdentifier)
{
	if (!LandscapePointer || LandscapePointer->IsPendingKill() || !LandscapePointer->LandscapeSoftPtr.IsValid())
		return;

	if (!InOutput || InOutput->IsPendingKill())
		return;

	UHoudiniAssetComponent * HAC = Cast<UHoudiniAssetComponent>(InOutput->GetOuter());
	if (!HAC || HAC->IsPendingKill())
		return;

	AActor * OwnerActor = HAC->GetOwner();
	if (!OwnerActor || OwnerActor->IsPendingKill())
		return;


	ALandscapeProxy * Landscape = LandscapePointer->LandscapeSoftPtr.Get();

	if (!Landscape || Landscape->IsPendingKill())
		return;

	// TODO: Get bake base name
	FString Label = Landscape->GetName();

	EHoudiniLandscapeOutputBakeType & LandscapeOutputBakeType = LandscapePointer->BakeType;

	// Get thumbnail pool for this builder
	IDetailLayoutBuilder & DetailLayoutBuilder = HouOutputCategory.GetParentLayout();
	TSharedPtr< FAssetThumbnailPool > AssetThumbnailPool = DetailLayoutBuilder.GetThumbnailPool();

	TArray<TSharedPtr<FString>>* BakeOptionString = FHoudiniEngineEditor::Get().GetHoudiniLandscapeOutputBakeOptionsLabels();

	// Create bake mesh name textfield.
	IDetailGroup& LandscapeGrp = HouOutputCategory.AddGroup(FName(*Label), FText::FromString(Label));
	LandscapeGrp.AddWidgetRow()
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("BakeBaseName", "Bake Name"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		.FillWidth(1)
		[
			SNew(SEditableTextBox)
			.Text(FText::FromString(Label))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ToolTipText(LOCTEXT("BakeNameTip", "The base name of the baked asset"))
			.HintText(LOCTEXT("BakeNameHintText", "Input bake name to override default"))
			.OnTextCommitted_Lambda([InOutput, OutputIdentifier](const FText& Val, ETextCommit::Type TextCommitType)
			{
				FHoudiniOutputDetails::OnBakeNameCommitted(Val, TextCommitType, InOutput, OutputIdentifier);
				FHoudiniEngineUtils::UpdateEditorProperties(InOutput, true);
			})
		]
		+ SHorizontalBox::Slot()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SButton)
			.ToolTipText(LOCTEXT("RevertNameOverride", "Revert bake name override"))
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.ContentPadding(0)
			.Visibility(EVisibility::Visible)
			.OnClicked_Lambda([InOutput, OutputIdentifier]() 
			{
				FHoudiniOutputDetails::OnRevertBakeNameToDefault(InOutput, OutputIdentifier);
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
			]
		]
	];

	// Create the thumbnail for the landscape output object.
	TSharedPtr< FAssetThumbnail > LandscapeThumbnail =
		MakeShareable(new FAssetThumbnail(Landscape, 64, 64, AssetThumbnailPool));

	TSharedPtr< SBorder > LandscapeThumbnailBorder;
	TSharedRef< SVerticalBox > VerticalBox = SNew(SVerticalBox);

	LandscapeGrp.AddWidgetRow()
	.NameContent()
	[
		SNew(SSpacer)
		.Size(FVector2D(250, 64))
	]
	.ValueContent()
	.MinDesiredWidth(HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH)
	[
		VerticalBox
	];

	VerticalBox->AddSlot().Padding(0, 2).AutoHeight()
	[
		SNew(SBox).WidthOverride(175)
		[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(0.0f, 0.0f, 2.0f, 0.0f)
		.AutoWidth()
		[
			SAssignNew(LandscapeThumbnailBorder, SBorder)
			.Padding(5.0f)
			//.BorderImage(this, &FHoudiniOutputDetails::GetLandscapeThumbnailBorder, Landscape)
			.OnMouseDoubleClick(this, &FHoudiniOutputDetails::OnThumbnailDoubleClick, (UObject *)Landscape)
			[
				SNew(SBox)
				.WidthOverride(64)
				.HeightOverride(64)
				.ToolTipText(FText::FromString(Landscape->GetPathName()))
				[
					LandscapeThumbnail->MakeThumbnailWidget()
				]
			]
		]

		+ SHorizontalBox::Slot()
		.Padding(0.0f, 4.0f, 4.0f, 4.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(40.0f)
			[
				SNew(SButton)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("Bake", "Bake"))
				.IsEnabled(true)
				//.OnClicked_Lambda(OnBakeLandscapeLambda)
				.OnClicked_Lambda([InOutput, OutputIdentifier, HAC, OwnerActor, HGPO, Landscape, LandscapeOutputBakeType]()
				{
					FHoudiniOutputObject* FoundOutputObject = InOutput->GetOutputObjects().Find(OutputIdentifier);
					if (FoundOutputObject)
					{
						FHoudiniOutputDetails::OnBakeOutputObject(
							FoundOutputObject->BakeName, Landscape, OutputIdentifier, HGPO, OwnerActor->GetName(), HAC->BakeFolder.Path, InOutput->GetType(), LandscapeOutputBakeType);
					}

					// TODO: Remove the output landscape if the landscape bake type is Detachment?
					return FReply::Handled();
				})
				.ToolTipText(LOCTEXT("HoudiniLandscapeBakeButton", "Bake this landscape"))	
			]	
		]

		+ SHorizontalBox::Slot()
		.Padding(0.0f, 4.0f, 4.0f, 4.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(120.f)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(FHoudiniEngineEditor::Get().GetHoudiniLandscapeOutputBakeOptionsLabels())
				.InitiallySelectedItem((*FHoudiniEngineEditor::Get().GetHoudiniLandscapeOutputBakeOptionsLabels())[(uint8)LandscapeOutputBakeType])
				.OnGenerateWidget_Lambda(
					[](TSharedPtr< FString > InItem)
				{
					return SNew(STextBlock).Text(FText::FromString(*InItem));
				})
				.OnSelectionChanged_Lambda(
					[LandscapePointer, InOutput](TSharedPtr< FString > NewChoice, ESelectInfo::Type SelectType)
				{
					if (SelectType != ESelectInfo::Type::OnMouseClick)
						return;

					FString *NewChoiceStr = NewChoice.Get();
					if (!NewChoiceStr)
						return;

					if (*NewChoiceStr == FHoudiniEngineEditorUtils::HoudiniLandscapeOutputBakeTypeToString(EHoudiniLandscapeOutputBakeType::Detachment))
					{
						LandscapePointer->SetLandscapeOutputBakeType(EHoudiniLandscapeOutputBakeType::Detachment);
					}
					else if (*NewChoiceStr == FHoudiniEngineEditorUtils::HoudiniLandscapeOutputBakeTypeToString(EHoudiniLandscapeOutputBakeType::BakeToImage))
					{
						LandscapePointer->SetLandscapeOutputBakeType(EHoudiniLandscapeOutputBakeType::BakeToImage);
					}
					else
					{
						LandscapePointer->SetLandscapeOutputBakeType(EHoudiniLandscapeOutputBakeType::BakeToWorld);
					}

					FHoudiniEngineUtils::UpdateEditorProperties(InOutput, true);
					})
					[
						SNew(STextBlock)
						.Text_Lambda([LandscapePointer]()
						{
							FString BakeTypeString = FHoudiniEngineEditorUtils::HoudiniLandscapeOutputBakeTypeToString(LandscapePointer->GetLandscapeOutputBakeType());
							return FText::FromString(BakeTypeString);
						})
						.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					]
				]		
			]
		]
	];

	// Store thumbnail for this landscape.
	LandscapeThumbnailBorders.Add(Landscape, LandscapeThumbnailBorder);


	// We need to add material box for each the landscape and landscape hole materials
	for (int32 MaterialIdx = 0; MaterialIdx < 2; ++MaterialIdx)
	{
		UMaterialInterface * MaterialInterface = MaterialIdx == 0 ? Landscape->GetLandscapeMaterial() : Landscape->GetLandscapeHoleMaterial();
		TSharedPtr< SBorder > MaterialThumbnailBorder;
		TSharedPtr< SHorizontalBox > HorizontalBox = NULL;

		FString MaterialName, MaterialPathName;
		if (MaterialInterface)
		{
			MaterialName = MaterialInterface->GetName();
			MaterialPathName = MaterialInterface->GetPathName();
		}

		// Create thumbnail for this material.
		TSharedPtr< FAssetThumbnail > MaterialInterfaceThumbnail =
			MakeShareable(new FAssetThumbnail(MaterialInterface, 64, 64, AssetThumbnailPool));

		VerticalBox->AddSlot().Padding(2, 2, 5, 2).AutoHeight()
		[
			SNew(STextBlock)
			.Text(MaterialIdx == 0 ? LOCTEXT("LandscapeMaterial", "Landscape Material") : LOCTEXT("LandscapeHoleMaterial", "Landscape Hole Material"))
			.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		];

		VerticalBox->AddSlot().Padding(0, 2)
		[
			SNew(SAssetDropTarget)
			.OnIsAssetAcceptableForDrop(this, &FHoudiniOutputDetails::OnMaterialInterfaceDraggedOver)
			//.OnAssetDropped(
			//	this, &FHoudiniOutputDetails::OnMaterialInterfaceDropped,
			//	Landscape, HGPO, MaterialIdx)
			[
				SAssignNew(HorizontalBox, SHorizontalBox)
			]
		];

		HorizontalBox->AddSlot().Padding(0.0f, 0.0f, 2.0f, 0.0f).AutoWidth()
		[
			SAssignNew(MaterialThumbnailBorder, SBorder)
			.Padding(5.0f)
			//.BorderImage(
			//	this, &FHoudiniOutputDetails::GetMaterialInterfaceThumbnailBorder, Landscape, MaterialIdx)
			.OnMouseDoubleClick(
				this, &FHoudiniOutputDetails::OnThumbnailDoubleClick, (UObject *)MaterialInterface)
			[
				SNew(SBox)
				.WidthOverride(64)
				.HeightOverride(64)
				.ToolTipText(FText::FromString(MaterialPathName))
				[
					MaterialInterfaceThumbnail->MakeThumbnailWidget()
				]
			]
		];

		// Store thumbnail for this mesh and material index.
		{
			TPairInitializer< ALandscapeProxy *, int32 > Pair(Landscape, MaterialIdx);
			LandscapeMaterialInterfaceThumbnailBorders.Add(Pair, MaterialThumbnailBorder);
		}

		TSharedPtr< SComboButton > AssetComboButton;
		TSharedPtr< SHorizontalBox > ButtonBox;

		HorizontalBox->AddSlot()
		.FillWidth(1.0f)
		.Padding(0.0f, 4.0f, 4.0f, 4.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			[
				SAssignNew(ButtonBox, SHorizontalBox)
				+ SHorizontalBox::Slot()
				[
					SAssignNew(AssetComboButton, SComboButton)
					//.ToolTipText( this, &FHoudiniAssetComponentDetails::OnGetToolTip )
					.ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
					.ForegroundColor(FEditorStyle::GetColor("PropertyEditor.AssetName.ColorAndOpacity"))
					//.OnGetMenuContent(this, &FHoudiniOutputDetails::OnGetMaterialInterfaceMenuContent,
					//	MaterialInterface, Landscape, HGPO, MaterialIdx)
					.ContentPadding(2.0f)
					.ButtonContent()
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "PropertyEditor.AssetClass")
						.Font(FEditorStyle::GetFontStyle(FName(TEXT("PropertyWindow.NormalFont"))))
						.Text(FText::FromString(MaterialName))
					]
				]
			]
		];

		// Create tooltip.
		FFormatNamedArguments Args;
		Args.Add(TEXT("Asset"), FText::FromString(MaterialName));
		FText MaterialTooltip = FText::Format(
			LOCTEXT("BrowseToSpecificAssetInContentBrowser", "Browse to '{Asset}' in Content Browser"), Args);

		ButtonBox->AddSlot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			PropertyCustomizationHelpers::MakeBrowseButton(
				FSimpleDelegate::CreateSP(
					this, &FHoudiniOutputDetails::OnBrowseTo, (UObject*)MaterialInterface),
				TAttribute< FText >(MaterialTooltip))
		];

		ButtonBox->AddSlot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ToolTipText(LOCTEXT("ResetToBaseMaterial", "Reset to base material"))
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.ContentPadding(0)
			.Visibility(EVisibility::Visible)
			//.OnClicked(
			//	this, &FHoudiniOutputDetails::OnResetMaterialInterfaceClicked,
			//	Landscape, &HoudiniGeoPartObject, MaterialIdx)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
			]
		];

		// Store combo button for this mesh and index.
		{
			TPairInitializer< ALandscapeProxy *, int32 > Pair(Landscape, MaterialIdx);
			LandscapeMaterialInterfaceComboButtons.Add(Pair, AssetComboButton);
		}
	}
		
}

void
FHoudiniOutputDetails::CreateMeshOutputWidget(
	IDetailCategoryBuilder& HouOutputCategory,
	UHoudiniOutput* InOutput)
{
	if (!InOutput || InOutput->IsPendingKill())
		return;

	UHoudiniAssetComponent* HAC = Cast<UHoudiniAssetComponent>(InOutput->GetOuter());
	if (!HAC || HAC->IsPendingKill())
		return;

	AActor * OwnerActor = HAC->GetOwner();
	if (!OwnerActor || OwnerActor->IsPendingKill())
		return;

	// Go through this output's object
	int32 OutputObjIdx = 0;
	TMap<FHoudiniOutputObjectIdentifier, FHoudiniOutputObject>& OutputObjects = InOutput->GetOutputObjects();
	for (auto& IterObject : OutputObjects)
	{
		UStaticMesh* StaticMesh = Cast<UStaticMesh>(IterObject.Value.OutputObject);
		UHoudiniStaticMesh* ProxyMesh = Cast<UHoudiniStaticMesh>(IterObject.Value.ProxyObject);

		if ((!StaticMesh || StaticMesh->IsPendingKill())
			&& (!ProxyMesh || ProxyMesh->IsPendingKill()))
			continue;

		FHoudiniOutputObjectIdentifier & OutputIdentifier = IterObject.Key;

		// Find the corresponding HGPO in the output
		FHoudiniGeoPartObject HoudiniGeoPartObject;
		for (const auto& curHGPO : InOutput->GetHoudiniGeoPartObjects())
		{
			if (!OutputIdentifier.Matches(curHGPO))
				continue;

			HoudiniGeoPartObject = curHGPO;
			break;
		}

		if (StaticMesh && !StaticMesh->IsPendingKill())
		{
			bool bIsProxyMeshCurrent = IterObject.Value.bProxyIsCurrent;

			// If we have a static mesh, alway display its widget even if the proxy is more recent
			CreateStaticMeshAndMaterialWidgets(
				HouOutputCategory, InOutput, StaticMesh, OutputIdentifier, OwnerActor->GetName(), HAC->BakeFolder.Path, HoudiniGeoPartObject, bIsProxyMeshCurrent);
		}
		else
		{
			// If we only have a proxy mesh, then show the proxy widget
			CreateProxyMeshAndMaterialWidgets(
				HouOutputCategory, InOutput, ProxyMesh, OutputIdentifier, OwnerActor->GetName(), HAC->BakeFolder.Path, HoudiniGeoPartObject);
		}		
	}
}

void 
FHoudiniOutputDetails::CreateCurveOutputWidget(IDetailCategoryBuilder& HouOutputCategory, UHoudiniOutput* InOutput) 
{
	if (!InOutput || InOutput->IsPendingKill())
		return;

	int32 OutputObjIdx = 0;
	TMap<FHoudiniOutputObjectIdentifier, FHoudiniOutputObject>& OutputObjects = InOutput->GetOutputObjects();
	for (auto& IterObject : OutputObjects)
	{
		FHoudiniOutputObject& CurrentOutputObject = IterObject.Value;
		USceneComponent* SplineComponent = Cast<USceneComponent>(IterObject.Value.OutputComponent);
		if (!SplineComponent || SplineComponent->IsPendingKill())
			continue;

		FHoudiniOutputObjectIdentifier& OutputIdentifier = IterObject.Key;
		FHoudiniGeoPartObject HoudiniGeoPartObject;
		for (const auto& curHGPO : InOutput->GetHoudiniGeoPartObjects()) 
		{
			if (!OutputIdentifier.Matches(curHGPO))
				continue;

			HoudiniGeoPartObject = curHGPO;
			break;
		}

		CreateCurveWidgets(HouOutputCategory, InOutput, SplineComponent, CurrentOutputObject, OutputIdentifier, HoudiniGeoPartObject);
	}
}

void 
FHoudiniOutputDetails::CreateCurveWidgets(
	IDetailCategoryBuilder& HouOutputCategory,
	UHoudiniOutput* InOutput,
	USceneComponent* SplineComponent,
	FHoudiniOutputObject& OutputObject,
	FHoudiniOutputObjectIdentifier& OutputIdentifier,
	FHoudiniGeoPartObject& HoudiniGeoPartObject) 
{
	if (!SplineComponent || SplineComponent->IsPendingKill())
		return;

	UHoudiniAssetComponent * HAC = Cast<UHoudiniAssetComponent>(InOutput->GetOuter());
	if (!HAC || HAC->IsPendingKill())
		return;

	AActor * OwnerActor = HAC->GetOwner();
	if (!OwnerActor || OwnerActor->IsPendingKill())
		return;

	FHoudiniCurveOutputProperties* OutputProperty = &(OutputObject.CurveOutputProperty);

	bool bIsUnrealSpline = OutputProperty->CurveOutputType == EHoudiniCurveOutputType::UnrealSpline;
	int32 NumPoints = OutputProperty->NumPoints;
	bool bIsClosed = OutputProperty->bClosed;
	EHoudiniCurveType CurveType = OutputProperty->CurveType;
	EHoudiniCurveMethod CurveMethod = OutputProperty->CurveMethod;

	FString Label = SplineComponent->GetName();
	if (HoudiniGeoPartObject.bHasCustomPartName)
		Label = HoudiniGeoPartObject.PartName;

	//Label += FString("_") + OutputIdentifier.SplitIdentifier;

	FString OutputCurveName = OutputObject.BakeName;
	if(OutputCurveName.IsEmpty())
		OutputCurveName = OwnerActor->GetName() + "_" + Label;

	// Hint text
	FString ExportAsStr = bIsUnrealSpline ? TEXT("Unreal spline") : TEXT("Houdini spline");

	const FText& LabelText = FText::FromString(ExportAsStr);

	IDetailGroup& CurveOutputGrp = HouOutputCategory.AddGroup(FName(*Label), FText::FromString(Label));

	// Bake name row UI
	CurveOutputGrp.AddWidgetRow()
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("BakeBaseName", "Bake Name"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		.FillWidth(1)
		[
			SNew(SEditableTextBox)
			.Text(FText::FromString(OutputObject.BakeName))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ToolTipText(LOCTEXT("BakeNameTip", "The base name of the baked asset"))
			.HintText(LOCTEXT("BakeNameHintText", "Input bake name to override default"))
			.OnTextCommitted_Lambda([InOutput, OutputIdentifier](const FText& Val, ETextCommit::Type TextCommitType)
			{
				FHoudiniOutputDetails::OnBakeNameCommitted(Val, TextCommitType, InOutput, OutputIdentifier);
				FHoudiniEngineUtils::UpdateEditorProperties(InOutput, true);
			})
		]

		+ SHorizontalBox::Slot()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SButton)
			.ToolTipText(LOCTEXT("RevertNameOverride", "Revert bake name override"))
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.ContentPadding(0)
			.Visibility(EVisibility::Visible)
			.OnClicked_Lambda([InOutput, OutputIdentifier]()
			{
				FHoudiniOutputDetails::OnRevertBakeNameToDefault(InOutput, OutputIdentifier);
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
			]
		]
	];

	FDetailWidgetRow& Row = CurveOutputGrp.AddWidgetRow();
	TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox);
	HorizontalBox->AddSlot().AutoWidth().Padding(2.0f, 0.0f)
	[
		SNew(STextBlock)
		.Text(LabelText)
		.ToolTipText_Lambda([bIsUnrealSpline, bIsClosed, NumPoints, CurveType, CurveMethod, Label]() 
		{
			FString ToolTipStr = FString::Printf(TEXT(" curve: %s\n Export type: %s\n num points: %d\n type: %s\n method: %s\n closed: %s \n (Type, method and closure are set to default values, since we do not have a way to get the corresponding info from HAPI now.)"),
					*Label,
					bIsUnrealSpline ? *(FString("Unreal Spline")) : *(FString("Houdini Spline")),
					NumPoints,
					*FHoudiniEngineEditorUtils::HoudiniCurveTypeToString(CurveType),
					*FHoudiniEngineEditorUtils::HoudiniCurveMethodToString(CurveMethod),
					bIsClosed ? *(FString("yes")) : *(FString("no")) );
		
			return FText::FromString(ToolTipStr);
		})
		.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
	];

	TSharedRef<SVerticalBox> VerticalBox = SNew(SVerticalBox);

	// Output curve type UI
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ComboBox;
	VerticalBox->AddSlot().Padding(2.0f, 2.0f, 5.0f, 2.0f)
	[
		SAssignNew(ComboBox, SComboBox<TSharedPtr<FString>>)
		.OptionsSource(FHoudiniEngineEditor::Get().GetHoudiniCurveOutputExportTypeLabels())
		.InitiallySelectedItem((*FHoudiniEngineEditor::Get().GetHoudiniCurveOutputExportTypeLabels())[(uint8)OutputProperty->CurveOutputType])
		.OnGenerateWidget_Lambda(
			[](TSharedPtr< FString > InItem)
		{
			return SNew(STextBlock).Text(FText::FromString(*InItem));
		})
		.OnSelectionChanged_Lambda(
			[OutputProperty, InOutput](TSharedPtr< FString > NewChoice, ESelectInfo::Type SelectType)
		{
			FString *NewChoiceStr = NewChoice.Get();
			if (!NewChoiceStr)
				return;

			if (*NewChoiceStr == "Unreal Spline") 
			{
				// It is already an Unreal spline
				if (OutputProperty->CurveOutputType == EHoudiniCurveOutputType::UnrealSpline)
					return;

				OutputProperty->CurveOutputType = EHoudiniCurveOutputType::UnrealSpline;
				FHoudiniEngineUtils::UpdateEditorProperties(InOutput, true);
			}
			else if (*NewChoiceStr == "Houdini Spline") 
			{
				// It is already a Houdini spline
				if (OutputProperty->CurveOutputType == EHoudiniCurveOutputType::HoudiniSpline)
					return;

				OutputProperty->CurveOutputType = EHoudiniCurveOutputType::HoudiniSpline;
				FHoudiniEngineUtils::UpdateEditorProperties(InOutput, true);
			}
		})
		[
			SNew(STextBlock)
			.Text_Lambda([bIsUnrealSpline]() 
			{ 
				if (bIsUnrealSpline)
					return FText::FromString(TEXT("Unreal Spline"));
				else
					return FText::FromString(TEXT("Houdini Spline"));
				
			})
			.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		]
	];

	// Temporary: Add a combo to choose between curve/linear type if the curve is exported as Unreal spline
	// TODO: need to find a way to get this info from HAPI
	if (OutputProperty->CurveOutputType == EHoudiniCurveOutputType::UnrealSpline) 
	{
		auto InitialSelectionLambda = [OutputProperty]() 
		{
			if (OutputProperty->CurveType == EHoudiniCurveType::Linear)
			{
				return (*FHoudiniEngineEditor::Get().GetUnrealOutputCurveTypeLabels())[0];
			}
			else 
			{
				return (*FHoudiniEngineEditor::Get().GetUnrealOutputCurveTypeLabels())[1];
			}
		};


		TSharedPtr<SComboBox<TSharedPtr<FString>>> UnrealCurveTypeComboBox;
		VerticalBox->AddSlot().Padding(2.0f, 2.0f, 5.0f, 2.0f)
		[
			SAssignNew(UnrealCurveTypeComboBox, SComboBox<TSharedPtr<FString>>)
			.OptionsSource(FHoudiniEngineEditor::Get().GetUnrealOutputCurveTypeLabels())
			.InitiallySelectedItem(InitialSelectionLambda())
			.OnGenerateWidget_Lambda(
				[](TSharedPtr< FString > InItem)
			{
				return SNew(STextBlock).Text(FText::FromString(*InItem));
			})
			.OnSelectionChanged_Lambda(
				[OutputProperty, InOutput](TSharedPtr< FString > NewChoice, ESelectInfo::Type SelectType)
			{
				FString *NewChoiceStr = NewChoice.Get();
				if (!NewChoiceStr)
					return;

				if (*NewChoiceStr == "Linear")
				{
					if (OutputProperty->CurveType == EHoudiniCurveType::Linear)
						return;

					OutputProperty->CurveType = EHoudiniCurveType::Linear;
					FHoudiniEngineUtils::UpdateEditorProperties(InOutput, true);
				}
				else if (*NewChoiceStr == "Curve")
				{
					if (OutputProperty->CurveType != EHoudiniCurveType::Linear)
						return;

					OutputProperty->CurveType = EHoudiniCurveType::Bezier;
					FHoudiniEngineUtils::UpdateEditorProperties(InOutput, true);
				}
			})
			[
				SNew(STextBlock)
				.Text_Lambda([OutputProperty]()
				{
					if (OutputProperty->CurveType == EHoudiniCurveType::Linear)
						return FText::FromString(TEXT("Linear"));
					else
						return FText::FromString(TEXT("Curve"));

				})
				.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			]
		];
	}

	// Bake button UI
	FText BakeText = FText::FromString("Bake");
	FString ToolTipStr;
	if (bIsUnrealSpline)
		ToolTipStr = "Bake to Unreal spline";
	else
		ToolTipStr = "Switch output type to Unreal Spline to Bake";
	TSharedPtr<SButton> Button;
	VerticalBox->AddSlot().Padding(1.0f, 2.0f, 4.0f, 2.0f)
	[
		SAssignNew(Button, SButton)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.Text(BakeText)
		.IsEnabled(bIsUnrealSpline)
		.ToolTipText(FText::FromString(ToolTipStr))
		.OnClicked_Lambda([InOutput, SplineComponent, OutputIdentifier, HoudiniGeoPartObject, HAC, OwnerActor, OutputCurveName]()
		{
			FHoudiniOutputDetails::OnBakeOutputObject(
				OutputCurveName, SplineComponent, OutputIdentifier,
				HoudiniGeoPartObject, OwnerActor->GetName(), 
				HAC->BakeFolder.Path, InOutput->GetType(), EHoudiniLandscapeOutputBakeType::InValid);

			return FReply::Handled();
		})
	];

	Row.NameWidget.Widget = HorizontalBox;
	Row.ValueWidget.Widget = VerticalBox;

	Row.ValueWidget.MinDesiredWidth(HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH);

}

void
FHoudiniOutputDetails::CreateStaticMeshAndMaterialWidgets(
	IDetailCategoryBuilder& HouOutputCategory,
	UHoudiniOutput* InOutput,
	UStaticMesh * StaticMesh,
	FHoudiniOutputObjectIdentifier& OutputIdentifier,
	const FString HoudiniAssetName,
	const FString BakeFolder,
	FHoudiniGeoPartObject& HoudiniGeoPartObject,
	const bool& bIsProxyMeshCurrent)
{
	if (!StaticMesh || StaticMesh->IsPendingKill())
		return;
	
	FHoudiniOutputObject* FoundOutputObject = InOutput->GetOutputObjects().Find(OutputIdentifier);
	FString BakeName = FoundOutputObject ? FoundOutputObject->BakeName : FString();

	// Get thumbnail pool for this builder.
	IDetailLayoutBuilder & DetailLayoutBuilder = HouOutputCategory.GetParentLayout();
	TSharedPtr<FAssetThumbnailPool> AssetThumbnailPool = DetailLayoutBuilder.GetThumbnailPool();

	// TODO: GetBakingBaseName!
	FString Label = StaticMesh->GetName();
	if (HoudiniGeoPartObject.bHasCustomPartName)
		Label = HoudiniGeoPartObject.PartName;

	// Create thumbnail for this mesh.
	TSharedPtr< FAssetThumbnail > StaticMeshThumbnail =
		MakeShareable(new FAssetThumbnail(StaticMesh, 64, 64, AssetThumbnailPool));
	TSharedPtr<SBorder> StaticMeshThumbnailBorder;

	TSharedRef<SVerticalBox> VerticalBox = SNew(SVerticalBox);

	IDetailGroup& StaticMeshGrp = HouOutputCategory.AddGroup(FName(*Label), FText::FromString(Label));
	StaticMeshGrp.AddWidgetRow()
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("BakeBaseName", "Bake Name"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		.FillWidth(1)
		[
			SNew(SEditableTextBox)
			.Text(FText::FromString(BakeName))
			.HintText(LOCTEXT("BakeNameHintText", "Input bake name to override default"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.OnTextCommitted_Lambda([OutputIdentifier, InOutput](const FText& Val, ETextCommit::Type TextCommitType)
			{
				FHoudiniOutputDetails::OnBakeNameCommitted(Val, TextCommitType, InOutput, OutputIdentifier);
				FHoudiniEngineUtils::UpdateEditorProperties(InOutput->GetOuter(), true);
			})
			.ToolTipText( LOCTEXT( "BakeNameTip", "The base name of the baked asset") )
        ]

        +SHorizontalBox::Slot()
        .Padding( 2.0f, 0.0f )
        .VAlign( VAlign_Center )
        .AutoWidth()
        [
            SNew( SButton )
            .ToolTipText( LOCTEXT( "RevertNameOverride", "Revert bake name override" ) )
            .ButtonStyle( FEditorStyle::Get(), "NoBorder" )
            .ContentPadding( 0 )
            .Visibility( EVisibility::Visible )
			.OnClicked_Lambda([InOutput, OutputIdentifier]() 
			{
				FHoudiniOutputDetails::OnRevertBakeNameToDefault(InOutput, OutputIdentifier);
				FHoudiniEngineUtils::UpdateEditorProperties(InOutput->GetOuter(), true);
				return FReply::Handled();
			})
            [
                SNew( SImage )
                .Image( FEditorStyle::GetBrush( "PropertyWindow.DiffersFromDefault" ) )
            ]
        ]
    ];

	// Add details on the SM colliders
	EHoudiniSplitType SplitType = FHoudiniMeshTranslator::GetSplitTypeFromSplitName(OutputIdentifier.SplitIdentifier);
    FString MeshLabel = TEXT( "Static Mesh" );

	// If the Proxy mesh is more recent, indicate it in the details
	if (bIsProxyMeshCurrent)
	{
		MeshLabel += TEXT("\n(unrefined)");
	}

	int32 NumSimpleColliders = 0;
	if (StaticMesh->BodySetup && !StaticMesh->BodySetup->IsPendingKill())
		NumSimpleColliders = StaticMesh->BodySetup->AggGeom.GetElementCount();

    if(NumSimpleColliders > 0)
    {
        MeshLabel += TEXT( "\n(") + FString::FromInt(NumSimpleColliders) + TEXT(" Simple Collider" );
        if (NumSimpleColliders > 1 )
            MeshLabel += TEXT("s");
        MeshLabel += TEXT(")");
    }
	else if (SplitType == EHoudiniSplitType::RenderedComplexCollider)
    {
        MeshLabel += TEXT( "\n(Rendered Complex Collider)" );
    }
    else if(SplitType == EHoudiniSplitType::InvisibleComplexCollider )
    {
        MeshLabel += TEXT( "\n(Invisible Complex Collider)" );
    }

    if ( StaticMesh->GetNumLODs() > 1 )
        MeshLabel += TEXT("\n(") + FString::FromInt( StaticMesh->GetNumLODs() ) + TEXT(" LODs)");

    if ( StaticMesh->Sockets.Num() > 0 )
        MeshLabel += TEXT("\n(") + FString::FromInt( StaticMesh->Sockets.Num() ) + TEXT(" sockets)");

	UHoudiniAssetComponent* HoudiniAssetComponent = Cast<UHoudiniAssetComponent>(InOutput->GetOuter());
    StaticMeshGrp.AddWidgetRow()
    .NameContent()
    [
        SNew( STextBlock )
        .Text( FText::FromString(MeshLabel) )
        .Font( IDetailLayoutBuilder::GetDetailFont() )
    ]
    .ValueContent()
    .MinDesiredWidth(HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH)
    [
        VerticalBox
    ];
            
    VerticalBox->AddSlot()
	.Padding( 0, 2 )
	.AutoHeight()
    [
        SNew( SHorizontalBox )
        +SHorizontalBox::Slot()
        .Padding( 0.0f, 0.0f, 2.0f, 0.0f )
        .AutoWidth()
        [
            SAssignNew( StaticMeshThumbnailBorder, SBorder )
            .Padding( 5.0f )
            .BorderImage( this, &FHoudiniOutputDetails::GetMeshThumbnailBorder, (UObject*)StaticMesh )
            .OnMouseDoubleClick( this, &FHoudiniOutputDetails::OnThumbnailDoubleClick, (UObject *) StaticMesh )
            [
                SNew( SBox )
                .WidthOverride( 64 )
                .HeightOverride( 64 )
                .ToolTipText( FText::FromString( StaticMesh->GetPathName() ) )
                [
                    StaticMeshThumbnail->MakeThumbnailWidget()
                ]
            ]
        ]

        +SHorizontalBox::Slot()
        .FillWidth( 1.0f )
        .Padding( 0.0f, 4.0f, 4.0f, 4.0f )
        .VAlign( VAlign_Center )
        [
            SNew( SVerticalBox )
            +SVerticalBox::Slot()
            [
                SNew( SHorizontalBox )
                +SHorizontalBox::Slot()
                .MaxWidth( 80.0f )
                [
                    SNew( SButton )
                    .VAlign( VAlign_Center )
                    .HAlign( HAlign_Center )
                    .Text( LOCTEXT( "Bake", "Bake" ) )
					.IsEnabled(true)
					.OnClicked_Lambda([BakeName, StaticMesh, OutputIdentifier, HoudiniGeoPartObject, HoudiniAssetName, BakeFolder, InOutput]()
					{
						FHoudiniOutputDetails::OnBakeOutputObject(BakeName, StaticMesh,
								OutputIdentifier, HoudiniGeoPartObject, HoudiniAssetName, 
								BakeFolder, InOutput->GetType(), EHoudiniLandscapeOutputBakeType::InValid);

						return FReply::Handled();
					})
                    .ToolTipText( LOCTEXT( "HoudiniStaticMeshBakeButton", "Bake this generated static mesh" ) )
                ]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					PropertyCustomizationHelpers::MakeBrowseButton(
						FSimpleDelegate::CreateSP(
							this, &FHoudiniOutputDetails::OnBrowseTo, (UObject*)StaticMesh),
							TAttribute<FText>(LOCTEXT("HoudiniStaticMeshBrowseButton", "Browse to this generated static mesh in the content browser")))
				]
            ]
        ]
    ];

    // Store thumbnail for this mesh.
    StaticMeshThumbnailBorders.Add( StaticMesh, StaticMeshThumbnailBorder );

    // We need to add material box for each material present in this static mesh.
    auto & StaticMeshMaterials = StaticMesh->StaticMaterials;
    for ( int32 MaterialIdx = 0; MaterialIdx < StaticMeshMaterials.Num(); ++MaterialIdx )
    {
        UMaterialInterface * MaterialInterface = StaticMeshMaterials[ MaterialIdx ].MaterialInterface;
        TSharedPtr< SBorder > MaterialThumbnailBorder;
        TSharedPtr< SHorizontalBox > HorizontalBox = NULL;

        FString MaterialName, MaterialPathName;
        if ( MaterialInterface && !MaterialInterface->IsPendingKill()
            && MaterialInterface->GetOuter() && !MaterialInterface->GetOuter()->IsPendingKill() )
        {
            MaterialName = MaterialInterface->GetName();
            MaterialPathName = MaterialInterface->GetPathName();
        }
        else
        {
            MaterialInterface = nullptr;
            MaterialName = TEXT("Material (invalid)") + FString::FromInt( MaterialIdx ) ;
            MaterialPathName = TEXT("Material (invalid)") + FString::FromInt(MaterialIdx);
        }

        // Create thumbnail for this material.
        TSharedPtr< FAssetThumbnail > MaterialInterfaceThumbnail =
            MakeShareable( new FAssetThumbnail( MaterialInterface, 64, 64, AssetThumbnailPool ) );

        VerticalBox->AddSlot().Padding( 0, 2 )
        [
            SNew( SAssetDropTarget )
            .OnIsAssetAcceptableForDrop( this, &FHoudiniOutputDetails::OnMaterialInterfaceDraggedOver )
            .OnAssetDropped(
                this, &FHoudiniOutputDetails::OnMaterialInterfaceDropped, StaticMesh, InOutput, MaterialIdx )
            [
                SAssignNew( HorizontalBox, SHorizontalBox )
            ]
        ];

        HorizontalBox->AddSlot().Padding( 0.0f, 0.0f, 2.0f, 0.0f ).AutoWidth()
        [
            SAssignNew( MaterialThumbnailBorder, SBorder )
            .Padding( 5.0f )
            .BorderImage(
                this, &FHoudiniOutputDetails::GetMaterialInterfaceThumbnailBorder, (UObject *)StaticMesh, MaterialIdx )
            .OnMouseDoubleClick(
                this, &FHoudiniOutputDetails::OnThumbnailDoubleClick, (UObject *)MaterialInterface )
            [
                SNew( SBox )
                .WidthOverride( 64 )
                .HeightOverride( 64 )
                .ToolTipText( FText::FromString( MaterialPathName ) )
                [
                    MaterialInterfaceThumbnail->MakeThumbnailWidget()
                ]
            ]
        ];

        // Store thumbnail for this mesh and material index.
        {
            TPairInitializer<UStaticMesh *, int32> Pair( StaticMesh, MaterialIdx );
            MaterialInterfaceThumbnailBorders.Add( Pair, MaterialThumbnailBorder );
        }

        TSharedPtr< SComboButton > AssetComboButton;
        TSharedPtr< SHorizontalBox > ButtonBox;

        HorizontalBox->AddSlot()
        .FillWidth( 1.0f )
        .Padding( 0.0f, 4.0f, 4.0f, 4.0f )
        .VAlign( VAlign_Center )
        [
            SNew( SVerticalBox )
            +SVerticalBox::Slot()
            .HAlign( HAlign_Fill )
            [
                SAssignNew( ButtonBox, SHorizontalBox )
                +SHorizontalBox::Slot()
                [
                    SAssignNew( AssetComboButton, SComboButton )
                    .ButtonStyle( FEditorStyle::Get(), "PropertyEditor.AssetComboStyle" )
                    .ForegroundColor( FEditorStyle::GetColor("PropertyEditor.AssetName.ColorAndOpacity" ) )
                    .OnGetMenuContent( this, &FHoudiniOutputDetails::OnGetMaterialInterfaceMenuContent,
                        MaterialInterface, StaticMesh, InOutput, MaterialIdx )
                    .ContentPadding( 2.0f )
                    .ButtonContent()
                    [
                        SNew( STextBlock )
                        .TextStyle( FEditorStyle::Get(), "PropertyEditor.AssetClass" )
                        .Font( FEditorStyle::GetFontStyle( FName( TEXT( "PropertyWindow.NormalFont" ) ) ) )
                        .Text( FText::FromString( MaterialName ) )
                    ]
                ]
            ]
        ];

        // Create tooltip.
        FFormatNamedArguments Args;
        Args.Add( TEXT( "Asset" ), FText::FromString( MaterialName ) );
        FText MaterialTooltip = FText::Format(
            LOCTEXT( "BrowseToSpecificAssetInContentBrowser", "Browse to '{Asset}' in Content Browser" ), Args );

        ButtonBox->AddSlot()
        .AutoWidth()
        .Padding( 2.0f, 0.0f )
        .VAlign( VAlign_Center )
        [
            PropertyCustomizationHelpers::MakeBrowseButton(
                FSimpleDelegate::CreateSP(
                    this, &FHoudiniOutputDetails::OnBrowseTo, (UObject*)MaterialInterface ), TAttribute< FText >( MaterialTooltip ) )
        ];

        ButtonBox->AddSlot()
        .AutoWidth()
        .Padding( 2.0f, 0.0f )
        .VAlign( VAlign_Center )
        [
            SNew( SButton )
            .ToolTipText( LOCTEXT( "ResetToBaseMaterial", "Reset to base material" ) )
            .ButtonStyle( FEditorStyle::Get(), "NoBorder" )
            .ContentPadding( 0 )
            .Visibility( EVisibility::Visible )
            .OnClicked(
				this, &FHoudiniOutputDetails::OnResetMaterialInterfaceClicked, StaticMesh, InOutput, MaterialIdx)
            [
                SNew( SImage )
                .Image( FEditorStyle::GetBrush( "PropertyWindow.DiffersFromDefault" ) )
            ]
        ];

        // Store combo button for this mesh and index.
        {
            TPairInitializer<UStaticMesh *, int32> Pair( StaticMesh, MaterialIdx );
            MaterialInterfaceComboButtons.Add( Pair, AssetComboButton );
        }
    }
}

void
FHoudiniOutputDetails::CreateProxyMeshAndMaterialWidgets(
	IDetailCategoryBuilder& HouOutputCategory,
	UHoudiniOutput* InOutput,
	UHoudiniStaticMesh * ProxyMesh,
	FHoudiniOutputObjectIdentifier& OutputIdentifier,
	const FString HoudiniAssetName,
	const FString BakeFolder,
	FHoudiniGeoPartObject& HoudiniGeoPartObject)
{
	if (!ProxyMesh || ProxyMesh->IsPendingKill())
		return;

	FHoudiniOutputObject* FoundOutputObject = InOutput->GetOutputObjects().Find(OutputIdentifier);
	FString BakeName = FoundOutputObject ? FoundOutputObject->BakeName : FString();

	// Get thumbnail pool for this builder.
	IDetailLayoutBuilder & DetailLayoutBuilder = HouOutputCategory.GetParentLayout();
	TSharedPtr<FAssetThumbnailPool> AssetThumbnailPool = DetailLayoutBuilder.GetThumbnailPool();

	// TODO: GetBakingBaseName!
	FString Label = ProxyMesh->GetName();
	if (HoudiniGeoPartObject.bHasCustomPartName)
		Label = HoudiniGeoPartObject.PartName;

	// Create thumbnail for this mesh.
	TSharedPtr<FAssetThumbnail> MeshThumbnail =	MakeShareable(new FAssetThumbnail(ProxyMesh, 64, 64, AssetThumbnailPool));
	TSharedPtr<SBorder> MeshThumbnailBorder;

	TSharedRef< SVerticalBox > VerticalBox = SNew(SVerticalBox);

	IDetailGroup& StaticMeshGrp = HouOutputCategory.AddGroup(FName(*Label), FText::FromString(Label));

	StaticMeshGrp.AddWidgetRow()
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("BakeBaseName", "Bake Name"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		.FillWidth(1)
		[
			SNew(SEditableTextBox)
			.Text(FText::FromString(BakeName))
			.HintText(LOCTEXT("BakeNameHintText", "Input bake name to override default"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.OnTextCommitted_Lambda([OutputIdentifier, InOutput](const FText& Val, ETextCommit::Type TextCommitType)
			{
				FHoudiniOutputDetails::OnBakeNameCommitted(Val, TextCommitType, InOutput, OutputIdentifier);
				FHoudiniEngineUtils::UpdateEditorProperties(InOutput->GetOuter(), true);
			})
			.ToolTipText(LOCTEXT("BakeNameTip", "The base name of the baked asset"))
		]
		+ SHorizontalBox::Slot()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SButton)
			.ToolTipText(LOCTEXT("RevertNameOverride", "Revert bake name override"))
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.ContentPadding(0)
			.Visibility(EVisibility::Visible)
			.OnClicked_Lambda([InOutput, OutputIdentifier]()
			{
				FHoudiniOutputDetails::OnRevertBakeNameToDefault(InOutput, OutputIdentifier);
				FHoudiniEngineUtils::UpdateEditorProperties(InOutput->GetOuter(), true);
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
			]
		]
	];

	// Add details on the Proxy Mesh
	EHoudiniSplitType SplitType = FHoudiniMeshTranslator::GetSplitTypeFromSplitName(OutputIdentifier.SplitIdentifier);
	FString MeshLabel = TEXT("Proxy Mesh");

	UHoudiniAssetComponent* HoudiniAssetComponent = Cast<UHoudiniAssetComponent>(InOutput->GetOuter());
	StaticMeshGrp.AddWidgetRow()
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString(MeshLabel))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH)
	[
		VerticalBox
	];

	VerticalBox->AddSlot()
	.Padding(0, 2)
	.AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(0.0f, 0.0f, 2.0f, 0.0f)
		.AutoWidth()
		[
			SAssignNew(MeshThumbnailBorder, SBorder)
			.Padding(5.0f)
			.BorderImage(this, &FHoudiniOutputDetails::GetMeshThumbnailBorder, (UObject*)ProxyMesh)
			.OnMouseDoubleClick(this, &FHoudiniOutputDetails::OnThumbnailDoubleClick, (UObject *)ProxyMesh)
			[
				SNew(SBox)
				.WidthOverride(64)
				.HeightOverride(64)
				.ToolTipText(FText::FromString(ProxyMesh->GetPathName()))
				[
					MeshThumbnail->MakeThumbnailWidget()
				]
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(0.0f, 4.0f, 4.0f, 4.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.MaxWidth(80.0f)
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("Refine", "Refine"))					
					.IsEnabled(true)
					.OnClicked(this, &FHoudiniOutputDetails::OnRefineClicked, (UObject *)ProxyMesh, InOutput)
					.ToolTipText(LOCTEXT("RefineTooltip", "Refine this Proxy Mesh to a Static Mesh"))
				]
			]
		]
	];

	// Store thumbnail for this mesh.
	StaticMeshThumbnailBorders.Add(ProxyMesh, MeshThumbnailBorder);

	// We need to add material box for each material present in this static mesh.
	auto & ProxyMeshMaterials = ProxyMesh->GetStaticMaterials();
	for (int32 MaterialIdx = 0; MaterialIdx < ProxyMeshMaterials.Num(); ++MaterialIdx)
	{
		UMaterialInterface * MaterialInterface = ProxyMeshMaterials[MaterialIdx].MaterialInterface;
		TSharedPtr< SBorder > MaterialThumbnailBorder;
		TSharedPtr< SHorizontalBox > HorizontalBox = NULL;

		FString MaterialName, MaterialPathName;
		if (MaterialInterface && !MaterialInterface->IsPendingKill()
			&& MaterialInterface->GetOuter() && !MaterialInterface->GetOuter()->IsPendingKill())
		{
			MaterialName = MaterialInterface->GetName();
			MaterialPathName = MaterialInterface->GetPathName();
		}
		else
		{
			MaterialInterface = nullptr;
			MaterialName = TEXT("Material (invalid)") + FString::FromInt(MaterialIdx);
			MaterialPathName = TEXT("Material (invalid)") + FString::FromInt(MaterialIdx);
		}

		// Create thumbnail for this material.
		TSharedPtr<FAssetThumbnail> MaterialInterfaceThumbnail =
			MakeShareable(new FAssetThumbnail(MaterialInterface, 64, 64, AssetThumbnailPool));

		// No drop target
		VerticalBox->AddSlot()
		.Padding(0, 2)
		[
			SNew(SAssetDropTarget)
			//.OnIsAssetAcceptableForDrop(false)
			//.OnAssetDropped(
			//	this, &FHoudiniOutputDetails::OnMaterialInterfaceDropped, StaticMesh, InOutput, MaterialIdx)
			[
				SAssignNew(HorizontalBox, SHorizontalBox)
			]
		];

		HorizontalBox->AddSlot()
		.Padding(0.0f, 0.0f, 2.0f, 0.0f)
		.AutoWidth()
		[
			SAssignNew(MaterialThumbnailBorder, SBorder)
			.Padding(5.0f)
			.BorderImage(
				this, &FHoudiniOutputDetails::GetMaterialInterfaceThumbnailBorder, (UObject*)ProxyMesh, MaterialIdx)
			.OnMouseDoubleClick(
				this, &FHoudiniOutputDetails::OnThumbnailDoubleClick, (UObject *)MaterialInterface)
			[
				SNew(SBox)
				.WidthOverride(64)
				.HeightOverride(64)
				.ToolTipText(FText::FromString(MaterialPathName))
				[
					MaterialInterfaceThumbnail->MakeThumbnailWidget()
				]
			]
		];

		// Store thumbnail for this mesh and material index.
		{
			TPairInitializer<UObject*, int32> Pair((UObject*)ProxyMesh, MaterialIdx);
			MaterialInterfaceThumbnailBorders.Add(Pair, MaterialThumbnailBorder);
		}
				
		TSharedPtr<SComboButton> AssetComboButton;
		TSharedPtr<SHorizontalBox> ButtonBox;
		HorizontalBox->AddSlot()
		.FillWidth(1.0f)
		.Padding(0.0f, 4.0f, 4.0f, 4.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			[
				SAssignNew(ButtonBox, SHorizontalBox)
				+ SHorizontalBox::Slot()
				[
					SAssignNew(AssetComboButton, SComboButton)
					.ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
					.ForegroundColor(FEditorStyle::GetColor("PropertyEditor.AssetName.ColorAndOpacity"))
					/*.OnGetMenuContent(this, &FHoudiniOutputDetails::OnGetMaterialInterfaceMenuContent,
						MaterialInterface, StaticMesh, InOutput, MaterialIdx)*/
					.ContentPadding(2.0f)
					.ButtonContent()
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "PropertyEditor.AssetClass")
						.Font(FEditorStyle::GetFontStyle(FName(TEXT("PropertyWindow.NormalFont"))))
						.Text(FText::FromString(MaterialName))
					]
				]
			]
		];
		
		// Disable the combobutton for proxies
		AssetComboButton->SetEnabled(false);

		// Create tooltip.
		FFormatNamedArguments Args;
		Args.Add(TEXT("Asset"), FText::FromString(MaterialName));
		FText MaterialTooltip = FText::Format(
			LOCTEXT("BrowseToSpecificAssetInContentBrowser", "Browse to '{Asset}' in Content Browser"), Args);

		ButtonBox->AddSlot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			PropertyCustomizationHelpers::MakeBrowseButton(
				FSimpleDelegate::CreateSP(this, &FHoudiniOutputDetails::OnBrowseTo, (UObject*)MaterialInterface), TAttribute<FText>(MaterialTooltip))
		];

		/*
		ButtonBox->AddSlot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ToolTipText(LOCTEXT("ResetToBaseMaterial", "Reset to base material"))
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.ContentPadding(0)
			.Visibility(EVisibility::Visible)
			.OnClicked(
				this, &FHoudiniOutputDetails::OnResetMaterialInterfaceClicked, StaticMesh, InOutput, MaterialIdx)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
			]
		];
		*/

		// Store combo button for this mesh and index.
		{
			TPairInitializer<UObject*, int32> Pair(ProxyMesh, MaterialIdx);
			MaterialInterfaceComboButtons.Add(Pair, AssetComboButton);
		}
	}
}

FText
FHoudiniOutputDetails::GetOutputDebugName(UHoudiniOutput* InOutput)
{
	// Get the name and type
	FString OutputNameStr = InOutput->GetName() + TEXT(" ") + UHoudiniOutput::OutputTypeToString(InOutput->GetType());

	// Then add the number of parts		
	OutputNameStr += TEXT(" (") + FString::FromInt(InOutput->GetHoudiniGeoPartObjects().Num()) + TEXT(" Part(s))\n");

	return FText::FromString(OutputNameStr);
}
FText
FHoudiniOutputDetails::GetOutputDebugDescription(UHoudiniOutput* InOutput)
{	
	const TArray<FHoudiniGeoPartObject>& HGPOs = InOutput->GetHoudiniGeoPartObjects();
	
	FString OutputValStr;
	OutputValStr += TEXT("HGPOs:\n");
	for (auto& HGPO : HGPOs)
	{
		OutputValStr += TEXT(" - ") + HGPO.PartName + TEXT(" (") + FHoudiniGeoPartObject::HoudiniPartTypeToString(HGPO.Type) + TEXT(")");

		if (HGPO.SplitGroups.Num() > 0)
		{
			OutputValStr += TEXT("( ") + FString::FromInt(HGPO.SplitGroups.Num()) + TEXT(" splits:");
			for (auto& split : HGPO.SplitGroups)
			{
				OutputValStr += TEXT(" ") + split;
			}
			OutputValStr += TEXT(")");
		}

		if (!HGPO.VolumeName.IsEmpty())
		{
			OutputValStr += TEXT("( ") + HGPO.VolumeName;
			if (HGPO.VolumeTileIndex >= 0)
				OutputValStr += TEXT(" tile ") + FString::FromInt(HGPO.VolumeTileIndex);
			OutputValStr += TEXT(" )");
		}

		OutputValStr += TEXT("\n");
	}

	// Add output objects if any
	TMap<FHoudiniOutputObjectIdentifier, FHoudiniOutputObject> AllOutputObj = InOutput->GetOutputObjects();
	if (AllOutputObj.Num() > 0)
	{
		bool TitleAdded = false;
		for (const auto& Iter : AllOutputObj)
		{
			UObject* OutObject = Iter.Value.OutputObject;
			if (OutObject)
			{
				OutputValStr += OutObject->GetFullName() + TEXT(" (obj)\n");
			}
			
			UObject* OutComp = Iter.Value.OutputComponent;
			if (OutComp)
			{
				OutputValStr += OutObject->GetFullName() + TEXT(" (comp)\n");
			}
		}
	}

	return FText::FromString(OutputValStr);
}

FText
FHoudiniOutputDetails::GetOutputTooltip(UHoudiniOutput* InOutput)
{
	// TODO
	return FText();
}


const FSlateBrush *
FHoudiniOutputDetails::GetMeshThumbnailBorder(UObject* Mesh) const
{
	TSharedPtr<SBorder> ThumbnailBorder = StaticMeshThumbnailBorders[Mesh];
	if (ThumbnailBorder.IsValid() && ThumbnailBorder->IsHovered())
		return FEditorStyle::GetBrush("PropertyEditor.AssetThumbnailLight");
	else
		return FEditorStyle::GetBrush("PropertyEditor.AssetThumbnailShadow");
}

/*
const FSlateBrush *
FHoudiniOutputDetails::GetLandscapeThumbnailBorder(ALandscapeProxy * Landscape) const
{
	TSharedPtr< SBorder > ThumbnailBorder = LandscapeThumbnailBorders[Landscape];
	if (ThumbnailBorder.IsValid() && ThumbnailBorder->IsHovered())
		return FEditorStyle::GetBrush("PropertyEditor.AssetThumbnailLight");
	else
		return FEditorStyle::GetBrush("PropertyEditor.AssetThumbnailShadow");
}
*/

const FSlateBrush *
FHoudiniOutputDetails::GetMaterialInterfaceThumbnailBorder(UObject* Mesh, int32 MaterialIdx) const
{
	if (!Mesh)
		return nullptr;

	TPairInitializer<UObject*, int32> Pair(Mesh, MaterialIdx);
	TSharedPtr<SBorder> ThumbnailBorder = MaterialInterfaceThumbnailBorders[Pair];

	if (ThumbnailBorder.IsValid() && ThumbnailBorder->IsHovered())
		return FEditorStyle::GetBrush("PropertyEditor.AssetThumbnailLight");
	else
		return FEditorStyle::GetBrush("PropertyEditor.AssetThumbnailShadow");
}

/*
const FSlateBrush *
FHoudiniOutputDetails::GetMaterialInterfaceThumbnailBorder(ALandscapeProxy * Landscape, int32 MaterialIdx) const
{
	if (!Landscape)
		return nullptr;

	TPairInitializer< ALandscapeProxy *, int32 > Pair(Landscape, MaterialIdx);
	TSharedPtr< SBorder > ThumbnailBorder = LandscapeMaterialInterfaceThumbnailBorders[Pair];

	if (ThumbnailBorder.IsValid() && ThumbnailBorder->IsHovered())
		return FEditorStyle::GetBrush("PropertyEditor.AssetThumbnailLight");
	else
		return FEditorStyle::GetBrush("PropertyEditor.AssetThumbnailShadow");
}
*/

FReply
FHoudiniOutputDetails::OnThumbnailDoubleClick(
	const FGeometry & InMyGeometry,
	const FPointerEvent & InMouseEvent, UObject * Object)
{
	if (Object && GEditor)
		GEditor->EditObject(Object);

	return FReply::Handled();
}

/*
FReply
FHoudiniOutputDetails::OnBakeStaticMesh(UStaticMesh * StaticMesh, UHoudiniAssetComponent * HoudiniAssetComponent, FHoudiniGeoPartObject& GeoPartObject)
{
	if (HoudiniAssetComponent && StaticMesh && !HoudiniAssetComponent->IsPendingKill() && !StaticMesh->IsPendingKill())
	{
		FHoudiniPackageParams PackageParms;


		FHoudiniEngineBakeUtils::BakeStaticMesh(HoudiniAssetComponent, GeoPartObject, StaticMesh, PackageParms);
		// TODO: Bake the SM

		
		// We need to locate corresponding geo part object in component.
		const FHoudiniGeoPartObject& HoudiniGeoPartObject = HoudiniAssetComponent->LocateGeoPartObject(StaticMesh);

		// (void)FHoudiniEngineBakeUtils::DuplicateStaticMeshAndCreatePackage(
		//	StaticMesh, HoudiniAssetComponent, HoudiniGeoPartObject, EBakeMode::ReplaceExisitingAssets);
		
	}

	return FReply::Handled();
}
*/

bool
FHoudiniOutputDetails::OnMaterialInterfaceDraggedOver(const UObject * InObject) const
{
	return (InObject && InObject->IsA(UMaterialInterface::StaticClass()));
}


FReply
FHoudiniOutputDetails::OnResetMaterialInterfaceClicked(
	UStaticMesh * StaticMesh,
	UHoudiniOutput * HoudiniOutput,
	int32 MaterialIdx)
{
	FReply RetValue = FReply::Handled();
	if (!StaticMesh || StaticMesh->IsPendingKill())
		return RetValue;

	if (!StaticMesh->StaticMaterials.IsValidIndex(MaterialIdx))
		return RetValue;

	// Retrieve material interface which is being replaced.
	UMaterialInterface * MaterialInterface = StaticMesh->StaticMaterials[MaterialIdx].MaterialInterface;
	if (!MaterialInterface)
		return RetValue;

	// Find the string corresponding to the material that is being replaced	
	const FString* FoundString = HoudiniOutput->GetReplacementMaterials().FindKey(MaterialInterface);
	if (!FoundString )
	{
		// This material was not replaced, no need to reset it
		return RetValue;
	}

	// This material has been replaced previously.
	FString MaterialString = *FoundString;

	// Record a transaction for undo/redo
	FScopedTransaction Transaction(
		TEXT(HOUDINI_MODULE_EDITOR),
		LOCTEXT("HoudiniMaterialReplacement", "Houdini Material Reset"), HoudiniOutput);

	// Remove the replacement
	HoudiniOutput->Modify();
	HoudiniOutput->GetReplacementMaterials().Remove(MaterialString);

	bool bViewportNeedsUpdate = true;

	// Try to find the original assignment, if not, we'll use the default material
	UMaterialInterface * AssignMaterial = FHoudiniEngine::Get().GetHoudiniDefaultMaterial().Get();
	UMaterialInterface * const * FoundMat = HoudiniOutput->GetAssignementMaterials().Find(MaterialString);
	if (FoundMat && (*FoundMat))
		AssignMaterial = *FoundMat;

	// Replace material on static mesh.
	StaticMesh->Modify();
	StaticMesh->StaticMaterials[MaterialIdx].MaterialInterface = AssignMaterial;

	// Replace the material on any component (SMC/ISMC) that uses the above SM
	// TODO: ?? Replace for all?
	for (auto& OutputObject : HoudiniOutput->GetOutputObjects())
	{
		// Only look at MeshComponents
		UStaticMeshComponent * SMC = Cast<UStaticMeshComponent>(OutputObject.Value.OutputComponent);
		if (!SMC)
			continue;

		if (SMC->GetStaticMesh() != StaticMesh)
			continue;

		SMC->Modify();
		SMC->SetMaterial(MaterialIdx, AssignMaterial);
	}

	FHoudiniEngineUtils::UpdateEditorProperties(HoudiniOutput->GetOuter(), true);

	if (GEditor)
		GEditor->RedrawAllViewports();

	return RetValue;
}

/*
FReply
FHoudiniOutputDetails::OnResetMaterialInterfaceClicked(
	ALandscapeProxy * Landscape, UHoudiniOutput * InOutput, int32 MaterialIdx)
{
	bool bViewportNeedsUpdate = false;

	// TODO: Handle me!
	for (TArray< UHoudiniAssetComponent * >::TIterator
		IterComponents(HoudiniAssetComponents); IterComponents; ++IterComponents)
	{
		UHoudiniAssetComponent * HoudiniAssetComponent = *IterComponents;
		if (!HoudiniAssetComponent)
			continue;

		TWeakObjectPtr<ALandscapeProxy>* FoundLandscapePtr = HoudiniAssetComponent->LandscapeComponents.Find(*HoudiniGeoPartObject);
		if (!FoundLandscapePtr)
			continue;

		ALandscapeProxy* FoundLandscape = FoundLandscapePtr->Get();
		if (!FoundLandscape || !FoundLandscape->IsValidLowLevel())
			continue;

		if (FoundLandscape != Landscape)
			continue;

		// Retrieve the material interface which is being replaced.
		UMaterialInterface * MaterialInterface = MaterialIdx == 0 ? Landscape->GetLandscapeMaterial() : Landscape->GetLandscapeHoleMaterial();
		UMaterialInterface * MaterialInterfaceReplacement = Cast<UMaterialInterface>(FHoudiniEngine::Get().GetHoudiniDefaultMaterial().Get());

		bool bMaterialRestored = false;
		FString MaterialShopName;
		if (!HoudiniAssetComponent->GetReplacementMaterialShopName(*HoudiniGeoPartObject, MaterialInterface, MaterialShopName))
		{
			// This material was not replaced so there's no need to reset it
			continue;
		}

		// Remove the replacement
		HoudiniAssetComponent->RemoveReplacementMaterial(*HoudiniGeoPartObject, MaterialShopName);

		// Try to find the original assignment, if not, we'll use the default material
		UMaterialInterface * AssignedMaterial = HoudiniAssetComponent->GetAssignmentMaterial(MaterialShopName);
		if (AssignedMaterial)
			MaterialInterfaceReplacement = AssignedMaterial;

		// Replace material on the landscape
		Landscape->Modify();

		if (MaterialIdx == 0)
			Landscape->LandscapeMaterial = MaterialInterfaceReplacement;
		else
			Landscape->LandscapeHoleMaterial = MaterialInterfaceReplacement;

		//Landscape->UpdateAllComponentMaterialInstances();

		// As UpdateAllComponentMaterialInstances() is not accessible to us, we'll try to access the Material's UProperty 
		// to trigger a fake Property change event that will call the Update function...
		UProperty* FoundProperty = FindField< UProperty >(Landscape->GetClass(), (MaterialIdx == 0) ? TEXT("LandscapeMaterial") : TEXT("LandscapeHoleMaterial"));
		if (FoundProperty)
		{
			FPropertyChangedEvent PropChanged(FoundProperty, EPropertyChangeType::ValueSet);
			Landscape->PostEditChangeProperty(PropChanged);
		}
		else
		{
			// The only way to update the material for now is to recook/recreate the landscape...
			HoudiniAssetComponent->StartTaskAssetCookingManual();
		}

		HoudiniAssetComponent->UpdateEditorProperties(false);
		bViewportNeedsUpdate = true;
	}

	if (GEditor && bViewportNeedsUpdate)
	{
		GEditor->RedrawAllViewports();
	}

	return FReply::Handled();
}
*/

void
FHoudiniOutputDetails::OnBrowseTo(UObject* InObject)
{
	if (GEditor)
	{
		TArray<UObject *> Objects;
		Objects.Add(InObject);
		GEditor->SyncBrowserToObjects(Objects);
	}
}

TSharedRef<SWidget>
FHoudiniOutputDetails::OnGetMaterialInterfaceMenuContent(
	UMaterialInterface * MaterialInterface,
	UStaticMesh * StaticMesh,
	UHoudiniOutput * InOutput,
	int32 MaterialIdx)
{
	TArray< const UClass * > AllowedClasses;
	AllowedClasses.Add(UMaterialInterface::StaticClass());

	TArray< UFactory * > NewAssetFactories;

	return PropertyCustomizationHelpers::MakeAssetPickerWithMenu(
		FAssetData(MaterialInterface),
		true,
		AllowedClasses,
		NewAssetFactories,
		OnShouldFilterMaterialInterface,
		FOnAssetSelected::CreateSP(
			this, &FHoudiniOutputDetails::OnMaterialInterfaceSelected, StaticMesh, InOutput, MaterialIdx),
		FSimpleDelegate::CreateSP(
			this, &FHoudiniOutputDetails::CloseMaterialInterfaceComboButton));
}

/*
TSharedRef< SWidget >
FHoudiniOutputDetails::OnGetMaterialInterfaceMenuContent(
	UMaterialInterface * MaterialInterface,
	ALandscapeProxy * Landscape,
	UHoudiniOutput * InOutput,
	int32 MaterialIdx)
{
	TArray< const UClass * > AllowedClasses;
	AllowedClasses.Add(UMaterialInterface::StaticClass());

	TArray< UFactory * > NewAssetFactories;

	return PropertyCustomizationHelpers::MakeAssetPickerWithMenu(
		FAssetData(MaterialInterface), true, AllowedClasses,
		NewAssetFactories, OnShouldFilterMaterialInterface,
		FOnAssetSelected::CreateSP(
			this, &FHoudiniOutputDetails::OnMaterialInterfaceSelected, Landscape, InOutput, MaterialIdx),
		FSimpleDelegate::CreateSP(
			this, &FHoudiniOutputDetails::CloseMaterialInterfaceComboButton));
}
*/

void
FHoudiniOutputDetails::CloseMaterialInterfaceComboButton()
{

}


void
FHoudiniOutputDetails::OnMaterialInterfaceDropped(
	UObject * InObject,
	UStaticMesh * StaticMesh,
	UHoudiniOutput * HoudiniOutput,
	int32 MaterialIdx)
{
	UMaterialInterface * MaterialInterface = Cast< UMaterialInterface >(InObject);
	if (!MaterialInterface || MaterialInterface->IsPendingKill())
		return;

	if (!StaticMesh || StaticMesh->IsPendingKill())
		return;

	if (!StaticMesh->StaticMaterials.IsValidIndex(MaterialIdx))
		return;

	bool bViewportNeedsUpdate = false;

	// Retrieve material interface which is being replaced.
	UMaterialInterface * OldMaterialInterface = StaticMesh->StaticMaterials[MaterialIdx].MaterialInterface;
	if (OldMaterialInterface == MaterialInterface)
		return;

	// Find the string corresponding to the material that is being replaced
	FString MaterialString = FString();
	const FString* FoundString = HoudiniOutput->GetReplacementMaterials().FindKey(OldMaterialInterface);
	if (FoundString)
	{
		// This material has been replaced previously.
		MaterialString = *FoundString;
	}
	else
	{
		// We have no previous replacement for this material,
		// see if we can find it the material assignment list.
		FoundString = HoudiniOutput->GetAssignementMaterials().FindKey(OldMaterialInterface);
		if (FoundString)
		{
			// This material has been assigned previously.
			MaterialString = *FoundString;
		}
		else
		{
			UMaterialInterface * DefaultMaterial = FHoudiniEngine::Get().GetHoudiniDefaultMaterial().Get();
			if (OldMaterialInterface == DefaultMaterial)
			{
				// This is replacement for default material.
				MaterialString = HAPI_UNREAL_DEFAULT_MATERIAL_NAME;
			}
			else
			{
				// External Material?
				MaterialString = OldMaterialInterface->GetName();
			}
		}
	}

	if (MaterialString.IsEmpty())
		return;

	// Record a transaction for undo/redo
	FScopedTransaction Transaction(
		TEXT(HOUDINI_MODULE_EDITOR),
		LOCTEXT("HoudiniMaterialReplacement", "Houdini Material Replacement"), HoudiniOutput);

	// Add a new material replacement entry.
	HoudiniOutput->Modify(); 
	HoudiniOutput->GetReplacementMaterials().Add(MaterialString, MaterialInterface);	

	// Replace material on static mesh.
	StaticMesh->Modify();
	StaticMesh->StaticMaterials[MaterialIdx].MaterialInterface = MaterialInterface;

	// Replace the material on any component (SMC/ISMC) that uses the above SM
	for (auto& OutputObject : HoudiniOutput->GetOutputObjects())
	{
		// Only look at MeshComponents
		UStaticMeshComponent * SMC = Cast<UStaticMeshComponent>(OutputObject.Value.OutputComponent);
		if (!SMC || SMC->IsPendingKill())
			continue;

		if (SMC->GetStaticMesh() != StaticMesh)
			continue;

		SMC->Modify();
		SMC->SetMaterial(MaterialIdx, MaterialInterface);
	}

	FHoudiniEngineUtils::UpdateEditorProperties(HoudiniOutput->GetOuter(), true);

	/*
	if(GUnrealEd)
		GUnrealEd->UpdateFloatingPropertyWindows();
*/
	if (GEditor)
		GEditor->RedrawAllViewports();
}

/*
void
FHoudiniOutputDetails::OnMaterialInterfaceDropped(
	UObject * InObject,
	ALandscapeProxy * Landscape,
	UHoudiniOutput * InOutput,
	int32 MaterialIdx)
{
	UMaterialInterface * MaterialInterface = Cast< UMaterialInterface >(InObject);
	if (!MaterialInterface || MaterialInterface->IsPendingKill())
		return;

	bool bViewportNeedsUpdate = false;

	
	// TODO: Handle replacement material
	// Replace material on component using this static mesh.
	for (TArray< UHoudiniAssetComponent * >::TIterator
		IterComponents(HoudiniAssetComponents); IterComponents; ++IterComponents)
	{
		UHoudiniAssetComponent * HoudiniAssetComponent = *IterComponents;
		if (!HoudiniAssetComponent || HoudiniAssetComponent->IsPendingKill())
			continue;

		TWeakObjectPtr<ALandscapeProxy>* FoundLandscapePtr = HoudiniAssetComponent->LandscapeComponents.Find(*HoudiniGeoPartObject);
		if (!FoundLandscapePtr || !FoundLandscapePtr->IsValid())
			continue;

		ALandscapeProxy* FoundLandscape = FoundLandscapePtr->Get();
		if (!FoundLandscape || !FoundLandscape->IsValidLowLevel())
			continue;

		if (FoundLandscape != Landscape)
			continue;

		// Retrieve the material interface which is being replaced.
		UMaterialInterface * OldMaterialInterface = MaterialIdx == 0 ? Landscape->GetLandscapeMaterial() : Landscape->GetLandscapeHoleMaterial();
		if (OldMaterialInterface == MaterialInterface)
			continue;

		// Record replaced material.
		const bool bReplaceSuccessful = HoudiniAssetComponent->ReplaceMaterial(
			*HoudiniGeoPartObject, MaterialInterface, OldMaterialInterface, MaterialIdx);

		if (!bReplaceSuccessful)
			continue;

		{
			FScopedTransaction Transaction(
				TEXT(HOUDINI_MODULE_EDITOR),
				LOCTEXT("HoudiniMaterialReplacement", "Houdini Material Replacement"), HoudiniAssetComponent);

			// Replace material on static mesh.
			Landscape->Modify();

			if (MaterialIdx == 0)
				Landscape->LandscapeMaterial = MaterialInterface;
			else
				Landscape->LandscapeHoleMaterial = MaterialInterface;

			//Landscape->UpdateAllComponentMaterialInstances();

			// As UpdateAllComponentMaterialInstances() is not accessible to us, we'll try to access the Material's UProperty 
			// to trigger a fake Property change event that will call the Update function...
			UProperty* FoundProperty = FindField< UProperty >(Landscape->GetClass(), (MaterialIdx == 0) ? TEXT("LandscapeMaterial") : TEXT("LandscapeHoleMaterial"));
			if (FoundProperty)
			{
				FPropertyChangedEvent PropChanged(FoundProperty, EPropertyChangeType::ValueSet);
				Landscape->PostEditChangeProperty(PropChanged);
			}
			else
			{
				// The only way to update the material for now is to recook/recreate the landscape...
				HoudiniAssetComponent->StartTaskAssetCookingManual();
			}
		}

		HoudiniAssetComponent->UpdateEditorProperties(false);
		bViewportNeedsUpdate = true;
	}
	

	if (GEditor && bViewportNeedsUpdate)
		GEditor->RedrawAllViewports();
}
*/

void
FHoudiniOutputDetails::OnMaterialInterfaceSelected(
	const FAssetData & AssetData,
	UStaticMesh * StaticMesh,
	UHoudiniOutput * InOutput,
	int32 MaterialIdx)
{
	TPairInitializer< UStaticMesh *, int32 > Pair(StaticMesh, MaterialIdx);
	TSharedPtr< SComboButton > AssetComboButton = MaterialInterfaceComboButtons[Pair];
	if (AssetComboButton.IsValid())
	{
		AssetComboButton->SetIsOpen(false);

		UObject * Object = AssetData.GetAsset();
		OnMaterialInterfaceDropped(Object, StaticMesh, InOutput, MaterialIdx);
	}
}

void
FHoudiniOutputDetails::CreateInstancerOutputWidget(
	IDetailCategoryBuilder& HouOutputCategory,
	UHoudiniOutput* InOutput)
{
	if (!InOutput || InOutput->IsPendingKill())
		return;

	// Classes allowed for instance variations.
	const TArray<const UClass *> AllowedClasses = 
	{
		UStaticMesh::StaticClass(), USkeletalMesh::StaticClass(),
		AActor::StaticClass(), UBlueprint::StaticClass(),
		UFXSystemAsset::StaticClass(), USoundBase::StaticClass()
	};

	// Classes not allowed for instances variations (useless?)
	TArray<const UClass *> DisallowedClasses =
	{
		UClass::StaticClass(), ULevel::StaticClass(), 
		UMaterial::StaticClass(), UTexture::StaticClass()
	};
	
	IDetailLayoutBuilder & DetailLayoutBuilder = HouOutputCategory.GetParentLayout();
	TSharedPtr<FAssetThumbnailPool> AssetThumbnailPool = DetailLayoutBuilder.GetThumbnailPool();

	// Lambda for adding new variation objects
	auto AddObjectAt = [InOutput](FHoudiniInstancedOutput& InOutputToUpdate, const int32& AtIndex, UObject* InObject)
	{	
		// TODO: undo/redo?
		InOutputToUpdate.VariationObjects.Insert(InObject, AtIndex);
		InOutputToUpdate.VariationTransformOffsets.Insert(FTransform::Identity, AtIndex);
		FHoudiniInstanceTranslator::UpdateVariationAssignements(InOutputToUpdate);

		InOutputToUpdate.MarkChanged(true);

		// Trigger an update?
		/*
		if (GUnrealEd)
			GUnrealEd->UpdateFloatingPropertyWindows();
		*/
		FHoudiniEngineUtils::UpdateEditorProperties(InOutput, true);
	};

	// Lambda for adding new geometry input objects
	auto RemoveObjectAt = [InOutput](FHoudiniInstancedOutput& InOutputToUpdate, const int32& AtIndex)
	{
		// Also keep one instance object
		if (AtIndex < 0 || AtIndex >= InOutputToUpdate.VariationObjects.Num())
			return;

		if (InOutputToUpdate.VariationObjects.Num() == 1)
			return;

		// TODO: undo/redo?

		InOutputToUpdate.VariationObjects.RemoveAt(AtIndex);
		InOutputToUpdate.VariationTransformOffsets.RemoveAt( AtIndex);
		FHoudiniInstanceTranslator::UpdateVariationAssignements(InOutputToUpdate);

		InOutputToUpdate.MarkChanged(true);

		// Trigger an update?
		/*
		if (GUnrealEd)
			GUnrealEd->UpdateFloatingPropertyWindows();
		*/
		FHoudiniEngineUtils::UpdateEditorProperties(InOutput, true);
	};

	// Lambda for updating a variation
	auto SetObjectAt = [InOutput](FHoudiniInstancedOutput& InOutputToUpdate, const int32& AtIndex, UObject* InObject)
	{
		if (!InOutputToUpdate.VariationObjects.IsValidIndex(AtIndex))
			return;

		InOutputToUpdate.VariationObjects[AtIndex] = InObject;

		InOutputToUpdate.MarkChanged(true);

		// Trigger an update?
		/*
		if (GUnrealEd)
			GUnrealEd->UpdateFloatingPropertyWindows();
		*/
		FHoudiniEngineUtils::UpdateEditorProperties(InOutput, true);
	};

	// Lambda for changing the transform offset values
	auto ChangeTransformOffsetAt = [InOutput](
		FHoudiniInstancedOutput& InOutputToUpdate, const int32& AtIndex, 
		const float& Value,  const int32& PosRotScaleIndex, const int32& XYZIndex)
	{
		bool bChanged = InOutputToUpdate.SetTransformOffsetAt(Value, AtIndex, PosRotScaleIndex, XYZIndex);
		if (!bChanged)
			return;

		InOutputToUpdate.MarkChanged(true);

		// Trigger an update?
		/*
		if (GUnrealEd)
			GUnrealEd->UpdateFloatingPropertyWindows();
		*/
		FHoudiniEngineUtils::UpdateEditorProperties(InOutput, true);
	};
		
	for (auto& Iter : InOutput->GetInstancedOutputs())
    {
		FHoudiniInstancedOutput& CurInstanceOutput = (Iter.Value);
        for( int32 VariationIdx = 0; VariationIdx < CurInstanceOutput.VariationObjects.Num(); VariationIdx++ )
        {
            UObject * InstancedObject = CurInstanceOutput.VariationObjects[VariationIdx].LoadSynchronous();
            if ( !InstancedObject || InstancedObject->IsPendingKill() )
            {
                HOUDINI_LOG_WARNING( TEXT("Null Object found for instance variation %d"), VariationIdx );
                continue;
            }

            // Create thumbnail for this object.
            TSharedPtr<FAssetThumbnail> VariationThumbnail =
                MakeShareable(new FAssetThumbnail(InstancedObject, 64, 64, AssetThumbnailPool));
            TSharedRef<SVerticalBox> PickerVerticalBox = SNew(SVerticalBox);
            TSharedPtr<SHorizontalBox> PickerHorizontalBox = nullptr;
            TSharedPtr<SBorder> VariationThumbnailBorder;

            //FString FieldLabel = InParam.GetFieldLabel(InstOutIdx, VariationIdx);
			FString InstanceOutputLabel = InOutput->GetName() + TEXT(" ") + UHoudiniOutput::OutputTypeToString(InOutput->GetType());
			InstanceOutputLabel += TEXT("_") + FString::FromInt(VariationIdx);
            			
			// Add a group for that variation
            IDetailGroup& DetailGroup = HouOutputCategory.AddGroup(FName(*InstanceOutputLabel), FText::FromString(InstanceOutputLabel));
            DetailGroup.AddWidgetRow()
            .NameContent()
            [
                SNew(SSpacer)
                .Size(FVector2D(250, 64))
            ]
            .ValueContent()
            .MinDesiredWidth(HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH)
            [
                PickerVerticalBox
            ];

			// Add an asset drop target
            PickerVerticalBox->AddSlot().Padding( 0, 2 ).AutoHeight()
            [
                SNew( SAssetDropTarget )
                .OnIsAssetAcceptableForDrop( SAssetDropTarget::FIsAssetAcceptableForDrop::CreateLambda( 
                    [=]( const UObject* Obj ) {
                        for ( auto Klass : DisallowedClasses )
                        {
                            if ( Obj && Obj->IsA( Klass ) )
                                return false;
                        }
                        return true;
                    })
                )
				.OnAssetDropped_Lambda([&CurInstanceOutput, VariationIdx, SetObjectAt](UObject* InObject)
				{
					return SetObjectAt(CurInstanceOutput, VariationIdx, InObject);
				})
                [
                    SAssignNew( PickerHorizontalBox, SHorizontalBox )
                ]
            ];

            PickerHorizontalBox->AddSlot().Padding(0.0f, 0.0f, 2.0f, 0.0f).AutoWidth()
            [
                SAssignNew(VariationThumbnailBorder, SBorder)
                .Padding( 5.0f )
				.OnMouseDoubleClick(this, &FHoudiniOutputDetails::OnThumbnailDoubleClick, InstancedObject)
                [
                    SNew(SBox)
                    .WidthOverride(64)
                    .HeightOverride(64)
                    .ToolTipText(FText::FromString(InstancedObject->GetPathName()))
                    [
						VariationThumbnail->MakeThumbnailWidget()
                    ]
                ]
            ];

			VariationThumbnailBorder->SetBorderImage(TAttribute< const FSlateBrush *>::Create(
                TAttribute<const FSlateBrush *>::FGetter::CreateLambda([=]()
				{
                    if (VariationThumbnailBorder.IsValid() && VariationThumbnailBorder->IsHovered())
                        return FEditorStyle::GetBrush("PropertyEditor.AssetThumbnailLight");
                    else
                        return FEditorStyle::GetBrush("PropertyEditor.AssetThumbnailShadow");
				}
			) ) );

            PickerHorizontalBox->AddSlot().AutoWidth().Padding(0.0f, 28.0f, 0.0f, 28.0f)
            [
                PropertyCustomizationHelpers::MakeAddButton(
					FSimpleDelegate::CreateLambda([&CurInstanceOutput, VariationIdx, AddObjectAt]()
					{				
						UObject* ObjToAdd = CurInstanceOutput.VariationObjects.IsValidIndex(VariationIdx) ?
							CurInstanceOutput.VariationObjects[VariationIdx].LoadSynchronous()
							: nullptr;

						return AddObjectAt(CurInstanceOutput, VariationIdx, ObjToAdd);
					}),
                    LOCTEXT("AddAnotherInstanceToolTip", "Add Another Instance"))
            ];

            PickerHorizontalBox->AddSlot().AutoWidth().Padding( 2.0f, 28.0f, 4.0f, 28.0f )
            [
                PropertyCustomizationHelpers::MakeRemoveButton(
					FSimpleDelegate::CreateLambda([&CurInstanceOutput, VariationIdx, RemoveObjectAt]()
					{
						return RemoveObjectAt(CurInstanceOutput, VariationIdx);
					}),
                    LOCTEXT("RemoveLastInstanceToolTip", "Remove Last Instance"))
            ];

            TSharedPtr<SComboButton> AssetComboButton;
            TSharedPtr<SHorizontalBox> ButtonBox;
            PickerHorizontalBox->AddSlot()
            .FillWidth(10.0f)
            .Padding(0.0f, 4.0f, 4.0f, 4.0f)
            .VAlign(VAlign_Center)
            [
                SNew(SVerticalBox)
                +SVerticalBox::Slot()
                .HAlign(HAlign_Fill)
                [
                    SAssignNew(ButtonBox, SHorizontalBox)
                    +SHorizontalBox::Slot()
                    [
                        SAssignNew(AssetComboButton, SComboButton)
                        //.ToolTipText( this, &FHoudiniAssetComponentDetails::OnGetToolTip )
                        .ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
                        .ForegroundColor( FEditorStyle::GetColor( "PropertyEditor.AssetName.ColorAndOpacity" ) )
                        /* TODO: Update UI
						.OnMenuOpenChanged( FOnIsOpenChanged::CreateUObject(
                            &InParam, &UHoudiniAssetInstanceInput::ChangedStaticMeshComboButton,
                            CurInstanceOutput, InstOutIdx, VariationIdx ) )
							*/
                        .ContentPadding(2.0f)
                        .ButtonContent()
                        [
                            SNew(STextBlock)
                            .TextStyle(FEditorStyle::Get(), "PropertyEditor.AssetClass")
                            .Font(FEditorStyle::GetFontStyle(FName(TEXT("PropertyWindow.NormalFont"))))
                            .Text(FText::FromString(InstancedObject->GetName()))
                        ]
                    ]
                ]
            ];

            // Create asset picker for this combo button.
            {
                TArray<UFactory *> NewAssetFactories;
                TSharedRef<SWidget> PropertyMenuAssetPicker = PropertyCustomizationHelpers::MakeAssetPickerWithMenu(
                    FAssetData(InstancedObject),
					true,
                    AllowedClasses,
					DisallowedClasses,
					NewAssetFactories,
					FOnShouldFilterAsset(),
                    FOnAssetSelected::CreateLambda([&CurInstanceOutput, VariationIdx, SetObjectAt, AssetComboButton](const FAssetData& AssetData)
					{
                        if ( AssetComboButton.IsValid() )
                        {
                            AssetComboButton->SetIsOpen( false );
                            UObject * Object = AssetData.GetAsset();
                            SetObjectAt( CurInstanceOutput, VariationIdx, Object);
                        }
                    }),
					// Nothing to do on close
					FSimpleDelegate::CreateLambda([](){})
				);

                AssetComboButton->SetMenuContent(PropertyMenuAssetPicker);
            }

            // Create tooltip.
            FFormatNamedArguments Args;
            Args.Add(TEXT("Asset"), FText::FromString(InstancedObject->GetName()));
            FText StaticMeshTooltip =
                FText::Format(LOCTEXT( "BrowseToSpecificAssetInContentBrowser", "Browse to '{Asset}' in Content Browser" ), Args);

            ButtonBox->AddSlot()
            .AutoWidth()
            .Padding(2.0f, 0.0f)
            .VAlign(VAlign_Center)
            [
                PropertyCustomizationHelpers::MakeBrowseButton(
					FSimpleDelegate::CreateLambda([&CurInstanceOutput, VariationIdx]()
					{
						UObject* InputObject = CurInstanceOutput.VariationObjects.IsValidIndex(VariationIdx) ?
							CurInstanceOutput.VariationObjects[VariationIdx].LoadSynchronous()
							: nullptr;

						if (GEditor && InputObject)
						{
							TArray<UObject*> Objects;
							Objects.Add(InputObject);
							GEditor->SyncBrowserToObjects(Objects);
						}
					}),
                    TAttribute< FText >( StaticMeshTooltip ) )
            ];

            ButtonBox->AddSlot()
            .AutoWidth()
            .Padding(2.0f, 0.0f )
            .VAlign(VAlign_Center)
            [
                SNew(SButton)
                .ToolTipText(LOCTEXT( "ResetToBase", "Reset to default static mesh"))
                .ButtonStyle(FEditorStyle::Get(), "NoBorder")
                .ContentPadding(0)
                .Visibility(EVisibility::Visible)
				.OnClicked_Lambda([SetObjectAt, &CurInstanceOutput, VariationIdx]()
				{
					SetObjectAt(CurInstanceOutput, VariationIdx, CurInstanceOutput.OriginalObject.LoadSynchronous());
					return FReply::Handled();
				})
                [
                    SNew(SImage)
                    .Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
                ]
            ];

			TSharedRef<SVerticalBox> OffsetVerticalBox = SNew(SVerticalBox);
            FText LabelPositionText = LOCTEXT("HoudiniPositionOffset", "Position Offset");
            DetailGroup.AddWidgetRow()
            .NameContent()
            [
                SNew(STextBlock)
                .Text(LabelPositionText)
                .ToolTipText(LabelPositionText)
                .Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
            ]
            .ValueContent()
            .MinDesiredWidth(HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH - 17)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().MaxWidth(HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH - 17)
                [
					SNew(SVectorInputBox)
					.bColorAxisLabels(true)
					.AllowSpin(true)
					.X(TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateLambda([&CurInstanceOutput, VariationIdx]()
							{ return CurInstanceOutput.GetTransformOffsetAt(VariationIdx, 0, 0); }
					)))
					.Y(TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateLambda([&CurInstanceOutput, VariationIdx]()
							{ return CurInstanceOutput.GetTransformOffsetAt(VariationIdx, 0, 1); }
					)))
					.Z(TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateLambda([&CurInstanceOutput, VariationIdx]()
							{ return CurInstanceOutput.GetTransformOffsetAt(VariationIdx, 0, 2); }
					)))
					.OnXCommitted_Lambda([&CurInstanceOutput, VariationIdx, ChangeTransformOffsetAt](float Val, ETextCommit::Type TextCommitType)
						{ ChangeTransformOffsetAt(CurInstanceOutput, VariationIdx, Val, 0, 0); })	
					.OnYCommitted_Lambda([&CurInstanceOutput, VariationIdx, ChangeTransformOffsetAt](float Val, ETextCommit::Type TextCommitType)
						{ ChangeTransformOffsetAt(CurInstanceOutput, VariationIdx, Val, 0, 1); })
					.OnZCommitted_Lambda([&CurInstanceOutput, VariationIdx, ChangeTransformOffsetAt](float Val, ETextCommit::Type TextCommitType)
						{ ChangeTransformOffsetAt(CurInstanceOutput, VariationIdx, Val, 0, 2); })
                ]
            ];

            FText LabelRotationText = LOCTEXT("HoudiniRotationOffset", "Rotation Offset");
            DetailGroup.AddWidgetRow()
            .NameContent()
            [
                SNew(STextBlock)
                .Text(LabelRotationText)
                .ToolTipText(LabelRotationText)
                .Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
            ]
            .ValueContent()
            .MinDesiredWidth(HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH - 17)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().MaxWidth(HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH - 17)
                [
                    SNew(SRotatorInputBox)
                    .AllowSpin(true)
                    .bColorAxisLabels(true)                    
					.Roll(TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateLambda([&CurInstanceOutput, VariationIdx]()
							{ return CurInstanceOutput.GetTransformOffsetAt(VariationIdx, 1, 0); }
					)))
					.Pitch(TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateLambda([&CurInstanceOutput, VariationIdx]()
							{ return CurInstanceOutput.GetTransformOffsetAt(VariationIdx, 1, 1); }
					)))
					.Yaw(TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateLambda([&CurInstanceOutput, VariationIdx]()
							{ return CurInstanceOutput.GetTransformOffsetAt(VariationIdx, 1, 2); }
					)))
					.OnRollCommitted_Lambda([&CurInstanceOutput, VariationIdx, ChangeTransformOffsetAt](float Val, ETextCommit::Type TextCommitType)
						{ ChangeTransformOffsetAt(CurInstanceOutput, VariationIdx, Val, 1, 0); })	
					.OnPitchCommitted_Lambda([&CurInstanceOutput, VariationIdx, ChangeTransformOffsetAt](float Val, ETextCommit::Type TextCommitType)
						{ ChangeTransformOffsetAt(CurInstanceOutput, VariationIdx, Val, 1, 1); })
					.OnYawCommitted_Lambda([&CurInstanceOutput, VariationIdx, ChangeTransformOffsetAt](float Val, ETextCommit::Type TextCommitType)
						{ ChangeTransformOffsetAt(CurInstanceOutput, VariationIdx, Val, 1, 2); })
                ]
            ];

            FText LabelScaleText = LOCTEXT("HoudiniScaleOffset", "Scale Offset");            
            DetailGroup.AddWidgetRow()
            .NameContent()
            [
                SNew(STextBlock)
                .Text(LabelScaleText)
                .ToolTipText(LabelScaleText)
                .Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
            ]
            .ValueContent()
            .MinDesiredWidth(HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().MaxWidth(HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH)
                [
                    SNew(SVectorInputBox)
                    .bColorAxisLabels(true)
					.X(TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateLambda([&CurInstanceOutput, VariationIdx]()
							{ return CurInstanceOutput.GetTransformOffsetAt(VariationIdx, 2, 0); }
					)))
					.Y(TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateLambda([&CurInstanceOutput, VariationIdx]()
							{ return CurInstanceOutput.GetTransformOffsetAt(VariationIdx, 2, 1); }
					)))
					.Z(TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateLambda([&CurInstanceOutput, VariationIdx]()
							{ return CurInstanceOutput.GetTransformOffsetAt(VariationIdx, 2, 2); }
					)))
					.OnXCommitted_Lambda([&CurInstanceOutput, VariationIdx, ChangeTransformOffsetAt](float Val, ETextCommit::Type TextCommitType)
						{ ChangeTransformOffsetAt(CurInstanceOutput, VariationIdx, Val, 2, 0); })	
					.OnYCommitted_Lambda([&CurInstanceOutput, VariationIdx, ChangeTransformOffsetAt](float Val, ETextCommit::Type TextCommitType)
						{ ChangeTransformOffsetAt(CurInstanceOutput, VariationIdx, Val, 2, 1); })
					.OnZCommitted_Lambda([&CurInstanceOutput, VariationIdx, ChangeTransformOffsetAt](float Val, ETextCommit::Type TextCommitType)
						{ ChangeTransformOffsetAt(CurInstanceOutput, VariationIdx, Val, 2, 2); })
                ]
                /*
				// TODO: Add support for this back
				+ SHorizontalBox::Slot().AutoWidth()
                [
                    // Add a checkbox to toggle between preserving the ratio of x,y,z components of scale when a value is entered
                    SNew(SCheckBox)
                    .Style(FEditorStyle::Get(), "TransparentCheckBox")
                    .ToolTipText(LOCTEXT("PreserveScaleToolTip", "When locked, scales uniformly based on the current xyz scale values so the object maintains its shape in each direction when scaled"))
                    /*
					.OnCheckStateChanged(FOnCheckStateChanged::CreateLambda([=](ECheckBoxState NewState)
					{
                        if ( MyParam.IsValid() && InputFieldPtr.IsValid() )
                            MyParam->CheckStateChanged( NewState == ECheckBoxState::Checked, InputFieldPtr.Get(), VariationIdx );
                    }))
                    .IsChecked( TAttribute< ECheckBoxState >::Create(
                        TAttribute<ECheckBoxState>::FGetter::CreateLambda( [=]() 
						{
                            if (InputFieldPtr.IsValid() && InputFieldPtr->AreOffsetsScaledLinearly(VariationIdx))
                                return ECheckBoxState::Checked;
                            return ECheckBoxState::Unchecked;
						}
					)))
					*//*					
                    [
                        SNew(SImage)
                        /*.Image(TAttribute<const FSlateBrush*>::Create(
                            TAttribute<const FSlateBrush*>::FGetter::CreateLambda( [=]() 
							{
								if ( InputFieldPtr.IsValid() && InputFieldPtr->AreOffsetsScaledLinearly( VariationIdx ) )
								{
									return FEditorStyle::GetBrush( TEXT( "GenericLock" ) );
								}
								return FEditorStyle::GetBrush( TEXT( "GenericUnlock" ) );
							}								
						)))
						*//*
                        .ColorAndOpacity( FSlateColor::UseForeground() )
                    ]
                ]
				*/
            ];
        }
    }
}

/*
void
FHoudiniOutputDetails::OnMaterialInterfaceSelected(
	const FAssetData & AssetData, 
	ALandscapeProxy* Landscape,
	UHoudiniOutput * InOutput,
	int32 MaterialIdx)
{
	TPairInitializer< ALandscapeProxy *, int32 > Pair(Landscape, MaterialIdx);
	TSharedPtr< SComboButton > AssetComboButton = LandscapeMaterialInterfaceComboButtons[Pair];
	if (AssetComboButton.IsValid())
	{
		AssetComboButton->SetIsOpen(false);

		UObject * Object = AssetData.GetAsset();
		OnMaterialInterfaceDropped(Object, Landscape, InOutput, MaterialIdx);
	}
}
*/

void
FHoudiniOutputDetails::CreateDefaultOutputWidget(
	IDetailCategoryBuilder& HouOutputCategory,
	UHoudiniOutput* InOutput)
{
	if (!InOutput)
		return;

	// Get thumbnail pool for this builder.
	TSharedPtr< FAssetThumbnailPool > AssetThumbnailPool = HouOutputCategory.GetParentLayout().GetThumbnailPool();

	// TODO
	// This is just a temporary placeholder displaying name/output type
	{
		FString OutputNameStr = InOutput->GetName();
		FText OutputTooltip = GetOutputTooltip(InOutput);

		// Create a new detail row
		// Name 
		FText OutputNameTxt = GetOutputDebugName(InOutput);
		FDetailWidgetRow & Row = HouOutputCategory.AddCustomRow(FText::GetEmpty());
		Row.NameWidget.Widget =
			SNew(STextBlock)
			.Text(OutputNameTxt)
			.ToolTipText(OutputTooltip)
			.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")));

		// Value
		FText OutputTypeTxt = GetOutputDebugDescription(InOutput);
		Row.ValueWidget.Widget =
			SNew(STextBlock)
			.Text(OutputTypeTxt)
			.ToolTipText(OutputTooltip)
			.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")));

		Row.ValueWidget.MinDesiredWidth(HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH);
	}
}

void
FHoudiniOutputDetails::OnBakeOutputObject(
	const FString& InBakeName,
	UObject * BakedOutputObject, 
	const FHoudiniOutputObjectIdentifier & OutputIdentifier,
	const FHoudiniGeoPartObject & HGPO,
	const FString & HoudiniAssetName,
	const FString & BakeFolder,
	const EHoudiniOutputType & Type,
	const EHoudiniLandscapeOutputBakeType & LandscapeBakeType)
{
	if (!BakedOutputObject || BakedOutputObject->IsPendingKill())
		return;

	FString ObjectName = InBakeName;
	

	// Set Object name according to priority  Default Name > Attrib Custom Name > UI Custom Name
	if(InBakeName.IsEmpty())
	{
		if (HGPO.bHasCustomPartName)
			ObjectName = HGPO.PartName;
		else
			ObjectName = BakedOutputObject->GetName();
	}

	// Fill in the package params
	FHoudiniPackageParams PackageParams;
	FHoudiniEngineBakeUtils::FillInPackageParamsForBakingOutput(
		PackageParams,
		OutputIdentifier,
		BakeFolder,
		ObjectName,
		HoudiniAssetName);


	switch (Type) 
	{
		case EHoudiniOutputType::Mesh:
		{
			UStaticMesh* StaticMesh = Cast<UStaticMesh>(BakedOutputObject);
			if (StaticMesh)	
				UStaticMesh* DuplicatedMesh = FHoudiniEngineBakeUtils::BakeStaticMesh(StaticMesh, PackageParams);
		}
		break;
		case EHoudiniOutputType::Curve:
		{
			USplineComponent* SplineComponent = Cast<USplineComponent>(BakedOutputObject);
			if (SplineComponent)
				FHoudiniEngineBakeUtils::BakeCurve(SplineComponent, PackageParams);
		}
		break;
		case EHoudiniOutputType::Landscape:
		{
			ALandscapeProxy* Landscape = Cast<ALandscapeProxy>(BakedOutputObject);
			if (Landscape)
			{
				FHoudiniEngineBakeUtils::BakeHeightfield(Landscape, PackageParams, LandscapeBakeType);
			}
		}
		break;
	}
}

FReply
FHoudiniOutputDetails::OnRefineClicked(UObject* ObjectToRefine, UHoudiniOutput* InOutput)
{	
	// TODO: Actually refine only the selected ProxyMesh
	// For now, refine all the selection
	FHoudiniEngineCommands::RefineHoudiniProxyMeshesToStaticMeshes(true, true);

	FHoudiniEngineUtils::UpdateEditorProperties(InOutput->GetOuter(), true);
	return FReply::Handled();
}

void
FHoudiniOutputDetails::OnBakeNameCommitted(
	const FText& Val, ETextCommit::Type TextCommitType,
	UHoudiniOutput * InOutput, const FHoudiniOutputObjectIdentifier& InIdentifier) 
{
	if (!InOutput)
		return;

	TMap<FHoudiniOutputObjectIdentifier, FHoudiniOutputObject>& OutputObjects = InOutput->GetOutputObjects();
	FHoudiniOutputObject* FoundOutputObject = OutputObjects.Find(InIdentifier);

	if (!FoundOutputObject)
		return;

	FoundOutputObject->BakeName = Val.ToString();
}

void
FHoudiniOutputDetails::OnRevertBakeNameToDefault(UHoudiniOutput * InOutput, const FHoudiniOutputObjectIdentifier & InIdentifier) 
{
	if (!InOutput)
		return;

	TMap<FHoudiniOutputObjectIdentifier, FHoudiniOutputObject>& OutputObjects = InOutput->GetOutputObjects();
	FHoudiniOutputObject* FoundOutputObject = OutputObjects.Find(InIdentifier);

	if (!FoundOutputObject)
		return;

	FoundOutputObject->BakeName = FString();
}
#undef LOCTEXT_NAMESPACE