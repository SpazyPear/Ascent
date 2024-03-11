// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Room.generated.h"

UINTERFACE(Blueprintable)
class ASCENT_API URoom : public UInterface
{
	GENERATED_BODY()
};

class ASCENT_API IRoom
{
	GENERATED_BODY()
	
public:	

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
		void GenerateRoom(uint8 width, uint8 length);
};
