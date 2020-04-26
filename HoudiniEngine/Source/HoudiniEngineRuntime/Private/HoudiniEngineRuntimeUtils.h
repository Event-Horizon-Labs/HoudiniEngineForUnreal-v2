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

class AActor;
class UWorld;

template<class TClass>
class TSubclassOf;

struct FBox;

struct HOUDINIENGINERUNTIME_API FHoudiniEngineRuntimeUtils
{
	public:

		// Return platform specific name of libHAPI.
		static FString GetLibHAPIName();

		// -----------------------------------------------
		// Bounding Box utilities
		// -----------------------------------------------

		// Collect all the bounding boxes form the specified list of actors. OutBBoxes will be emptied.
		static void GetBoundingBoxesFromActors(const TArray<AActor*> InActors, TArray<FBox>& OutBBoxes);

		// Collect actors that derive from the given class that intersect with the given array of bounding boxes.
		static bool FindActorsOfClassInBounds(UWorld* World, TSubclassOf<AActor> ActorType, const TArray<FBox>& BBoxes, const TArray<AActor*>* ExcludeActors, TArray<AActor*>& OutActors);
};