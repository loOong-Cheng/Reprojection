#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TeleopLatencyModel.h"
#include "TeleopLogger.h"
#include "TeleopTypes.h"
#include "TeleopGameMode.generated.h"

class ARemoteHand;
class ARobotHead;
class ATeleopOperatorPawn;
class ATeleopObserver;
class ATeleopOperatorRoom;
class ATeleopPresenter;
class ATeleopProps;

/**
 * Session coordinator. The Template map stays the remote world; this game mode:
 *  - spawns the operator (ATeleopOperatorPawn) in the dark room (spawning the room if the level has none),
 *  - spawns the robot side at the robot station: ARobotHead, two ARemoteHand, and the ATeleopPresenter,
 *  - owns the operator->remote mapping T_map = T_calib * T_station (remote = local * T_map) and recentering,
 *  - owns the latency model.
 */
UCLASS()
class REPROJECTION_API ATeleopGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATeleopGameMode();

	virtual AActor* FindPlayerStart_Implementation(AController* Player, const FString& IncomingName) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Maps the operator's current head (yaw, and floor position) onto the robot station. Keeps floor height (1:1).
	 *  Ignored while the head is not tracked. Runs automatically once tracking is valid (FTeleopAutoRecenter), again
	 *  after the headset is put on and after a system recenter. */
	UFUNCTION(BlueprintCallable, Category = "Teleop")
	void Recenter();

	/** HUD line while no valid calibration exists (empty when calibrated). */
	FString GetCalibrationWarning() const;

	/** Operator tracking frame -> remote world (remote = local * T_map). */
	FTransform GetLocalToRemote() const { return Calibration * GetStationTransform(); }

	FTransform GetStationTransform() const { return FTransform(FRotator(0.f, RobotStationYaw, 0.f), RobotStationLocation); }

	/** Operator eye height at calibration, cm above the floor; the robot's neutral camera height. */
	float GetCalibratedEyeHeight() const { return CalibratedEyeHeight; }

	void HandleGrabInput(bool bLeft, bool bPressed);

	FTeleopLatencyModel& GetLatencyModel() { return LatencyModel; }

	/** Per-session CSV logger (null before the session starts). */
	FTeleopLogger* GetLogger() const { return Logger.Get(); }

	UFUNCTION(BlueprintPure, Category = "Teleop")
	ATeleopProps* GetProps() const { return Props; }

	UFUNCTION(BlueprintPure, Category = "Teleop")
	ARobotHead* GetRobotHead() const { return RobotHead; }

	UFUNCTION(BlueprintPure, Category = "Teleop")
	ATeleopPresenter* GetPresenter() const { return Presenter; }

	UFUNCTION(BlueprintPure, Category = "Teleop")
	ARemoteHand* GetRemoteHand(bool bLeft) const { return bLeft ? LeftHand : RightHand; }

	/** Third-person observer view for the PC window. */
	UFUNCTION(BlueprintPure, Category = "Teleop")
	ATeleopObserver* GetObserver() const { return Observer; }

	/** Where the operator room is spawned when the level has none, cm (about 1 km from the Template scene). */
	UPROPERTY(EditDefaultsOnly, Category = "Teleop")
	FVector OperatorRoomLocation = FVector(-100000.f, 0.f, 0.f);

	/** Robot station on the Template floor: the seated operator's head maps above it, facing RobotStationYaw.
	 *  Default: 40 cm in front of the Template desk's near edge (x = 100). */
	UPROPERTY(EditDefaultsOnly, Category = "Teleop")
	FVector RobotStationLocation = FVector(60.f, 0.f, 0.f);

	UPROPERTY(EditDefaultsOnly, Category = "Teleop")
	float RobotStationYaw = 0.f;

private:
	ATeleopOperatorRoom* FindOrSpawnOperatorRoom();
	void SetupSession(ATeleopOperatorPawn* Pawn);

	UPROPERTY(Transient) TObjectPtr<ATeleopOperatorRoom> OperatorRoom;
	UPROPERTY(Transient) TObjectPtr<ATeleopOperatorPawn> OperatorPawn;
	UPROPERTY(Transient) TObjectPtr<ARobotHead> RobotHead;
	UPROPERTY(Transient) TObjectPtr<ARemoteHand> LeftHand;
	UPROPERTY(Transient) TObjectPtr<ARemoteHand> RightHand;
	UPROPERTY(Transient) TObjectPtr<ATeleopPresenter> Presenter;
	UPROPERTY(Transient) TObjectPtr<ATeleopProps> Props;
	UPROPERTY(Transient) TObjectPtr<ATeleopObserver> Observer;

	FTeleopLatencyModel LatencyModel;
	TUniquePtr<FTeleopLogger> Logger;

	void RearmAutoRecenter(const TCHAR* Reason);

	FTransform Calibration = FTransform::Identity;
	float CalibratedEyeHeight = 120.f;
	bool bCalibrated = false;
	FTeleopAutoRecenter AutoRecenter;
	FDelegateHandle PutOnHeadHandle;
	FDelegateHandle SystemRecenterHandle;
};
