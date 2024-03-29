﻿// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlumFPSCharacter.h"
#include "PlumFPSProjectile.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/InputSettings.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "MotionControllerComponent.h"
#include "XRMotionControllerBase.h" // for FXRMotionControllerBase::RightHandSourceId
#include "DrawDebugHelpers.h"
#include "PlumFPSHUD.h"
#include "PlumFPSGameMode.h"
#include <random>

DEFINE_LOG_CATEGORY_STATIC(LogFPChar, Warning, All);

//////////////////////////////////////////////////////////////////////////
// APlumFPSCharacter

APlumFPSCharacter::APlumFPSCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);


	//AmainWeapon* main{ NewObject<AmainWeapon>(GetTransientPackage(), AmainWeapon::StaticClass()) };
	//mainWeapon = GetWorld()->SpawnActor<AmainWeapon>(AmainWeapon::StaticClass(), Sp);

	//AsubWeapon* sub{ NewObject<AsubWeapon>(GetTransientPackage(), AsubWeapon::StaticClass()) };
	//subWeapon = sub;

	//currentWeapon = mainWeapon;
	//currentWeapon = subWeapon;

	mainWeapon.magazine = 30;
	mainWeapon.remainAmmo = 120;
	mainWeapon.coefYawRecoil = 1000;
	mainWeapon.coefPitchRecoil = 500;
	mainWeapon.zoomScale = 0.5f;
	mainWeapon.fireRate = 0.2f;
	mainWeapon.bulletSpread = 2000;
	mainWeapon.damage = 40;
	mainWeapon.canFullAutoFire = true;

	subWeapon.magazine = 12;
	subWeapon.remainAmmo = 60;
	subWeapon.coefYawRecoil = 1000;
	subWeapon.coefPitchRecoil = 500;
	subWeapon.zoomScale = 0.8f;
	subWeapon.fireRate = 0.2f;
	subWeapon.bulletSpread = 2000;
	subWeapon.damage = 40;
	subWeapon.canFullAutoFire = false;

	currentWeapon = subWeapon;

	 //set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	loadedAmmo = currentWeapon.magazine;
	remainAmmo = currentWeapon.remainAmmo;
	magazine = currentWeapon.magazine;

	isReloading = false;
	isAiming = false;
	isFiring = false;

	// Create a CameraComponent	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(-39.56f, 1.75f, 64.f)); // Position the camera
	FirstPersonCameraComponent->bUsePawnControlRotation = true;

	// Create a mesh component that will be used when being viewed from a '1st person' view (when controlling this pawn)
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->SetupAttachment(FirstPersonCameraComponent);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	Mesh1P->SetRelativeRotation(FRotator(1.9f, -19.19f, 5.2f));
	Mesh1P->SetRelativeLocation(FVector(-0.5f, -4.4f, -155.7f));

	// Create a gun mesh component
	FP_Gun = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FP_Gun"));
	FP_Gun->SetOnlyOwnerSee(false);			// otherwise won't be visible in the multiplayer
	FP_Gun->bCastDynamicShadow = false;
	FP_Gun->CastShadow = false;
	// FP_Gun->SetupAttachment(Mesh1P, TEXT("GripPoint"));
	FP_Gun->SetupAttachment(RootComponent);

	FP_MuzzleLocation = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzleLocation"));
	FP_MuzzleLocation->SetupAttachment(FP_Gun);
	FP_MuzzleLocation->SetRelativeLocation(FVector(0.2f, 48.4f, -10.6f));

	// Default offset from the character location for projectiles to spawn
	GunOffset = FVector(100.0f, 0.0f, 10.0f);

	TraceDistance = 2000.0f;

	// 자신 이외 모두가 일반 몸통 메시를 볼 수 있습니다.
	GetMesh()->SetOwnerNoSee(true);
}

void APlumFPSCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	//Attach gun mesh component to Skeleton, doing it here because the skeleton is not yet created in the constructor
	FP_Gun->AttachToComponent(Mesh1P, FAttachmentTransformRules(EAttachmentRule::SnapToTarget, true), TEXT("GripPoint"));
}

void APlumFPSCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// set up gameplay key bindings
	check(PlayerInputComponent);

	// Bind jump events
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	// Bind fire event
	PlayerInputComponent->BindAction("Fire", IE_Pressed, this, &APlumFPSCharacter::FullAutoFire);
	PlayerInputComponent->BindAction("Fire", IE_Released, this, &APlumFPSCharacter::StopFire);

	// Bind reload event
	PlayerInputComponent->BindAction("Reload", IE_Pressed, this, &APlumFPSCharacter::Reload);

	PlayerInputComponent->BindAction("Melee", IE_Pressed, this, &APlumFPSCharacter::DelayMelee);

	// Bind ads event
	PlayerInputComponent->BindAction("Ads", IE_Pressed, this, &APlumFPSCharacter::Ads);

	// Bind movement events
	PlayerInputComponent->BindAxis("MoveForward", this, &APlumFPSCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &APlumFPSCharacter::MoveRight);

	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
}

void APlumFPSCharacter::FullAutoFire()
{
	isFiring = true;
	OnFire();
	if (currentWeapon.canFullAutoFire)
	{
		GetWorld()->GetTimerManager().SetTimer(fireTimer, this, &APlumFPSCharacter::OnFire, currentWeapon.fireRate, true); // 0.*f : 연사율, true : 반복
	}
}

void APlumFPSCharacter::DelayMelee()
{
	GetWorld()->GetTimerManager().SetTimer(meleeTimer, this, &APlumFPSCharacter::Melee, 0.5f, false);
}

void APlumFPSCharacter::StopFire()
{
	isFiring = false;
	GetWorld()->GetTimerManager().ClearTimer(fireTimer);
}

void APlumFPSCharacter::OnFire()
{
	if (loadedAmmo <= 0 || isReloading == true) { return; }

	loadedAmmo -= 1;
	UE_LOG(LogTemp, Log, TEXT("Current Ammo : %d / %d"), loadedAmmo, remainAmmo);

	UWorld* const World = GetWorld();
	if (World != nullptr)
	{
		FRotator SpawnRotation = GetControlRotation();
		// MuzzleOffset is in camera space, so transform it to world space before offsetting from the character location to find the final muzzle position
		FVector SpawnLocation = ((FP_MuzzleLocation != nullptr) ? FP_MuzzleLocation->GetComponentLocation() : GetActorLocation()) + SpawnRotation.RotateVector(GunOffset);
		FHitResult hit;

		GetController()->GetPlayerViewPoint(SpawnLocation, SpawnRotation);

		FVector Start = SpawnLocation;
		FVector End;

		if (isAiming) 
		{
			End = Start + (SpawnRotation.Vector() * TraceDistance);

			std::random_device rd;
			std::mt19937 recoil(rd());
			std::uniform_int_distribution<int> yawRecoil(-50, 50);
			std::uniform_int_distribution<int> pitchRecoil(-100, 0);

			AddControllerYawInput(float(yawRecoil(recoil)) / currentWeapon.coefYawRecoil);
			AddControllerPitchInput(float(pitchRecoil(recoil)) / currentWeapon.coefPitchRecoil);
		}
		else // not aiming fire
		{
			std::random_device rd;
			std::mt19937 gen(rd());
			std::uniform_int_distribution<int> dis(-50, 50);
			// -50, 50, 2000 <- change this numbers, you can change spread range of bullet
			End = Start + (((SpawnRotation.Vector() + FVector(float(dis(gen)) / currentWeapon.bulletSpread, float(dis(gen)) / currentWeapon.bulletSpread, float(dis(gen)) / currentWeapon.bulletSpread)) * TraceDistance));

			std::mt19937 recoil(rd());
			std::uniform_int_distribution<int> yawRecoil(-50, 50);
			std::uniform_int_distribution<int> pitchRecoil(-100, 0);

			AddControllerYawInput(float(yawRecoil(recoil)) / currentWeapon.coefYawRecoil);
			AddControllerPitchInput(float(pitchRecoil(recoil)) / currentWeapon.coefPitchRecoil);
		}

		FCollisionQueryParams TraceParams;

		TraceParams.AddIgnoredActor(this);
    
		bool bHit = World->LineTraceSingleByChannel(hit, Start, End, ECC_Visibility, TraceParams);

		DrawDebugLine(World, Start, End, FColor::Blue, false, 2.0f);

		if (bHit)
		{
			if (hit.Actor->ActorHasTag("head"))
			{
				UE_LOG(LogTemp, Log, TEXT("HeadShot!"));
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("BodyShot!"));
			}
			UE_LOG(LogTemp, Log, TEXT("%s"), *hit.GetActor()->GetName());
      
      FDamageEvent DamageEvent;
			hit.Actor->TakeDamage(10.0f, DamageEvent, GetController(), this);
      
			DrawDebugBox(World, hit.ImpactPoint, FVector(5, 5, 5), FColor::Emerald, false, 2.0f);
		}

		if (!HasAuthority())
		{
			Server_OnFire(Start, End);
		}
		else
		{
			Multi_OnFire(Start, End);
		}
	}

	// try and play the sound if specified
	if (FireSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, GetActorLocation());
	}

	// try and play a firing animation if specified
	if (FireAnimation != nullptr)
	{
		// Get the animation object for the arms mesh
		UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance();
		if (AnimInstance != nullptr)
		{
			AnimInstance->Montage_Play(FireAnimation, 1.f);
		}
	}
}

bool APlumFPSCharacter::Server_OnFire_Validate(FVector Start, FVector End)
{
	return true;
}

void APlumFPSCharacter::Server_OnFire_Implementation(FVector Start, FVector End)
{
	UE_LOG(LogTemp, Warning, TEXT("Server_OnFire_Implementation HAS BEEN CALLED"));

	Multi_OnFire(Start, End);
}

bool APlumFPSCharacter::Multi_OnFire_Validate(FVector Start, FVector End)
{
	return true;
}

void APlumFPSCharacter::Multi_OnFire_Implementation(FVector Start, FVector End)
{
	UE_LOG(LogTemp, Warning, TEXT("Multi_OnFire_Implementation HAS BEEN CALLED"));

	if (!IsLocallyControlled())
	{
		FHitResult hit;
		FCollisionQueryParams TraceParams;
		bool bHit = GetWorld()->LineTraceSingleByChannel(hit, Start, End, ECC_Visibility, TraceParams);

		DrawDebugLine(GetWorld(), Start, End, FColor::Blue, false, 2.0f);

		if (bHit)
		{
			FDamageEvent DamageEvent;
			hit.Actor->TakeDamage(10.0f, DamageEvent, GetController(), this);
			DrawDebugBox(GetWorld(), hit.ImpactPoint, FVector(5, 5, 5), FColor::Emerald, false, 2.0f);
      
			if (hit.Actor->ActorHasTag("head"))
			{
				UE_LOG(LogTemp, Log, TEXT("HeadShot!"));
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("BodyShot!"));
			}
			UE_LOG(LogTemp, Log, TEXT("%s"), *hit.GetActor()->GetName());
			DrawDebugBox(GetWorld(), hit.ImpactPoint, FVector(5, 5, 5), FColor::Emerald, false, 2.0f);
		}

		if (FireSound != nullptr)
		{
			UGameplayStatics::PlaySoundAtLocation(this, FireSound, GetActorLocation());
		}

		if (FireAnimation != nullptr)
		{
			// Get the animation object for the arms mesh
			UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance();
			if (AnimInstance != nullptr)
			{
				AnimInstance->Montage_Play(FireAnimation, 1.f);
			}
		}
	}
}

void APlumFPSCharacter::Reload()
{
	if (isReloading == false && loadedAmmo != magazine && remainAmmo != 0)
	{
		isReloading = true;
		UE_LOG(LogTemp, Log, TEXT("Start Reloading"));

		GetWorld()->GetTimerManager().SetTimer(reloadTimer, this, &APlumFPSCharacter::ReloadDelay, 1.0f, false);

		if (remainAmmo <= 0 || loadedAmmo >= magazine) { return; }

		if (remainAmmo < (magazine - loadedAmmo))
		{
			loadedAmmo = loadedAmmo + remainAmmo;
			remainAmmo = 0;
		}
		else
		{
			remainAmmo = remainAmmo - (magazine - loadedAmmo);
			loadedAmmo = magazine;
		}
	}
}

void APlumFPSCharacter::Melee()
{
	UWorld* const World = GetWorld();
	if (World != nullptr)
	{
		FRotator SpawnRotation = GetControlRotation();
		// MuzzleOffset is in camera space, so transform it to world space before offsetting from the character location to find the final muzzle position
		FVector SpawnLocation = ((FP_MuzzleLocation != nullptr) ? FP_MuzzleLocation->GetComponentLocation() : GetActorLocation()) + SpawnRotation.RotateVector(GunOffset);
		FHitResult hit;

		GetController()->GetPlayerViewPoint(SpawnLocation, SpawnRotation);

		FVector Start = SpawnLocation;
		FVector End = Start + (SpawnRotation.Vector() * 200.0f);

		FCollisionQueryParams TraceParams;
		TraceParams.AddIgnoredActor(this);
		bool bHit = World->LineTraceSingleByChannel(hit, Start, End, ECC_Visibility, TraceParams);

		DrawDebugLine(World, Start, End, FColor::Blue, false, 2.0f);

		if (bHit)
		{
			if (hit.Actor->ActorHasTag("head"))
			{
				UE_LOG(LogTemp, Log, TEXT("HeadShot!"));
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("BodyShot!"));
			}
			UE_LOG(LogTemp, Log, TEXT("%s"), *hit.GetActor()->GetName());
			DrawDebugBox(World, hit.ImpactPoint, FVector(5, 5, 5), FColor::Emerald, false, 2.0f);
		}
	}
}


void APlumFPSCharacter::Ads()
{
	if (isReloading == false)
	{
		APlumFPSHUD* HU = Cast<APlumFPSHUD>(UGameplayStatics::GetPlayerController(this, 0)->GetHUD());

		if (isAiming == false)
		{
			isAiming = true;
			Mesh1P->SetHiddenInGame(true);
			FP_Gun->SetHiddenInGame(true);
			FirstPersonCameraComponent->FieldOfView *= currentWeapon.zoomScale;

			HU->setAds();
		}
		else
		{
			isAiming = false;
			Mesh1P->SetHiddenInGame(false);
			FP_Gun->SetHiddenInGame(false);
			FirstPersonCameraComponent->FieldOfView = 90.0f;

			HU->setNormal();
		}
	}
}

void APlumFPSCharacter::MoveForward(float Value)
{
	if (Value != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(GetActorForwardVector(), Value);
	}
}

void APlumFPSCharacter::MoveRight(float Value)
{
	if (Value != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(GetActorRightVector(), Value);
	}
}

void APlumFPSCharacter::ReloadDelay()
{
	isReloading = false;
	UE_LOG(LogTemp, Log, TEXT("Reloading Complete\nCurrent Ammo : %d / %d"), loadedAmmo, remainAmmo);
	GetWorldTimerManager().ClearTimer(reloadTimer);
}

void APlumFPSCharacter::SetCharacterHP(int32 hp) {
	CharacterHP = hp;
}

int32 APlumFPSCharacter::GetCharacterHP() {
	return CharacterHP;
}

/*
void APlumFPSCharacter::TakeDamage(int32 damage) {
	int32 HP = GetCharacterHP();

	HP -= damage;

	SetCharacterHP(HP);
}*/

float APlumFPSCharacter::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,class AController* EventInstigator, AActor* DamageCauser) {
	float FinalDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	int32 HP = GetCharacterHP();

	HP -= FinalDamage;

	SetCharacterHP(HP);

	UE_LOG(LogTemp, Warning, TEXT("Get Damaged %s : %d / %d "), * GetName(), CharacterHP, DefaultHP);

	return FinalDamage;
}