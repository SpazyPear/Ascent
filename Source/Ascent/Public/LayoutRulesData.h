// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LayoutRules.h"
#include "LayoutRulesData.generated.h"

/**
 * 
 */
UCLASS()
class ASCENT_API ULayoutRulesData : public UDataAsset
{
	GENERATED_BODY()
	
	public:
		UPROPERTY(EditDefaultsOnly)
			FLayoutRules LayoutRules;
};
