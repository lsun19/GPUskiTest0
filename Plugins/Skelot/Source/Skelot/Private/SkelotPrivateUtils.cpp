// Copyright 2024 Lazy Marmot Games. All Rights Reserved.

#include "SkelotPrivateUtils.h"



FString SkelotStructExportText(const UScriptStruct* StructPtr, const void* InstancePtr)
{
	FString DebugText;
	StructPtr->ExportText(DebugText, InstancePtr, nullptr, nullptr, 0, nullptr, true);
	return DebugText;
}
