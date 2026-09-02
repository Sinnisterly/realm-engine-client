# NameMapping vs RuntimeOffsets

| MultiTool logical | Realm class | Realm field / note | RuntimeOffsets row |
|---|---|---|---|
| `basicMapObjectClass::objectProperties` | `KJMONHENJEN` | `OBAKMCCDBJA` | ObjProps (yes) |
| `basicMapObjectClass::objectType` | `KJMONHENJEN` | `HFDNHJFNEKA` | ObjType (yes) |
| `basicMapObjectClass::x` | `KJMONHENJEN` | `CLFEOFKBNEJ` | PosX (yes) |
| `basicMapObjectClass::y` | `KJMONHENJEN` | `PKEECFNFEIO` | PosY (yes) |
| `mapObjectClass::conditions` | `LKHPPBEGNOM` | `COHCKAPOLCA` | MoConditions (yes) |
| `mapObjectClass::hp` | `LKHPPBEGNOM` | `KJNHLADHEMH` | HP (yes) |
| `mapObjectClass::maxHp` | `LKHPPBEGNOM` | `NCBIICBDGAG` | MaxHP (yes) |
| `mapObjectClass::moveVec` | `LKHPPBEGNOM` | `ECGPFJKCCAN` | MoVelocity (yes) |
| `mapViewServiceClass::mapObjectDictionary` | `HJMBOMEHGDJ` | `DFALIKKKGLI` | WM_AllDict (yes) |
| `mapViewServiceClass::player` | `HJMBOMEHGDJ` | `OCLNLBHDEFK` | WM_Local (yes) |
| `objectPropertiesClass::isEnemy` | `ObjectProperties` | `isEnemy` | OP_IsEnemy (yes) |
| `objectPropertiesClass::maxHitPoints` | `ObjectProperties` | `(via XML)` | OP + stats (yes) |
| `objectPropertiesClass::projectiles` | `ObjectProperties` | `Projectiles` | OP_Projectiles (yes) |
| `playerClass::shooting` | `FKALGHJIADI` | `(shooting class)` | AimHooks player/shoot (yes) |
| `playerClass::speed` | `FKALGHJIADI` | `CGCMALPMMJL / move speed` | SpeedHack (yes) |
| `projectileClass::_square` | `KJMONHENJEN` | `EOKJOGFPLOA` | ProjNoclip tile ptr (yes) |
| `projectileClass::damagesEnemies` | `HBEAKBIHANL` | `DBNNDLKNECM` | Hbeak_InstanceDamage (yes) |
| `projectilePropertiesClass::accelDelay` | `ProjectileProperties` | `AccelerationDelayValue` | PP_AccelDelay (yes) |
| `projectilePropertiesClass::accelInv` | `ProjectileProperties` | `AccelerationInv` | PP_AccelerationInv (yes) |
| `projectilePropertiesClass::accelValue` | `ProjectileProperties` | `AccelerationValue` | PP_Acceleration (yes) |
| `projectilePropertiesClass::isAccel` | `ProjectileProperties` | `IsAccelerating` | PP_IsAccel (yes) |
| `projectilePropertiesClass::lifetime` | `ProjectileProperties` | `Lifetime` | PP_Lifetime (yes) |
| `projectilePropertiesClass::speed` | `ProjectileProperties` | `ProjectileSpeed` | PP_Speed (yes) |
| `projectilePropertiesClass::speedClamp` | `ProjectileProperties` | `SpeedClampValue` | PP_SpeedClamp (yes) |

## MultiTool-only logical fields (no direct RuntimeOffsets row yet)

- `applicationManagerClass::assetLibraryService`
- `applicationManagerClass::cameraManager`
- `applicationManagerClass::mapViewService`
- `applicationManagerClass::settingsManager`
- `assetLibraryServiceClass::gameLibraries`
- `basicMapObjectClass::dead`
- `basicMapObjectClass::isMe`
- `cameraManagerClass::camera`
- `gameLibrariesClass::objectLibrary`
- `mapObjectClass::equipment`
- `objectLibraryClass::objects`
- `objectPropertiesClass::alwaysPositiveHealth`
- `objectPropertiesClass::isBoss`
- `objectPropertiesClass::isInvincible`
- `playerClass::lifeMul`
- `playerClass::speedMul`
- `squareClass::currentObjectType`

Mapped: 24/41
MultiTool-only: 17
