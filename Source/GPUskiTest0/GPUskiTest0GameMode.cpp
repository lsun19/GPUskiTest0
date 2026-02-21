// Copyright Epic Games, Inc. All Rights Reserved.

#include "GPUskiTest0GameMode.h"
#include "GPUskiTest0Character.h"
#include "UObject/ConstructorHelpers.h"

AGPUskiTest0GameMode::AGPUskiTest0GameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
