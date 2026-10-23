// Generated from splat-ref/src/config.js and game/kits/{bow,blade,mitts}.js. Do not edit by hand.
#pragma once
namespace restoration::test {
struct BowSourceFixture {
  double statsRange, statsDamage, statsRate, statsMobility;
  double statsPaint, chargeTime, ring1, inkFull;
  double inkMin, range, flightTap0, flightTap1;
  double flightRing0, flightRing1, flightFull, speedTap;
  double speedRing0, speedRing1, speedFull, straight;
  double grav, diveGrav, diveDrag, damageTap0;
  double damageTap1, damageRing0, damageRing1, damageFull;
  double sideMul, sideFull, fanTap, fanRing;
  double fanFull, fuseRing, fuseFull, burstRadius0;
  double burstRadius1, burstInner, burstDamage0, burstDamage1;
  double burstEdge0, burstEdge1, paintTap, paintStick;
  double burstPaint0, burstPaint1, trailEvery, trailRadius;
  double cooldown, moveSpeedDrawing, moveSpeedFiring, specialCost;
};
inline constexpr BowSourceFixture bowSourceFixture{
  0.84999999999999998, 0.59999999999999998, 0.45000000000000001, 0.50000000000000000,
  0.59999999999999998, 1.0000000000000000, 0.45000000000000001, 16.000000000000000,
  0.14999999999999999, 25.000000000000000, 5.0000000000000000, 7.5000000000000000,
  12.500000000000000, 16.500000000000000, 21.000000000000000, 32.000000000000000,
  42.000000000000000, 48.000000000000000, 58.000000000000000, 0.050000000000000003,
  6.0000000000000000, 140.00000000000000, 6.0000000000000000, 14.000000000000000,
  20.000000000000000, 30.000000000000000, 38.000000000000000, 55.000000000000000,
  0.80000000000000004, 45.000000000000000, 8.0000000000000000, 6.0000000000000000,
  4.5000000000000000, 0.69999999999999996, 0.55000000000000004, 1.3500000000000001,
  1.8000000000000000, 0.59999999999999998, 30.000000000000000, 45.000000000000000,
  12.000000000000000, 15.000000000000000, 0.55000000000000004, 0.41999999999999998,
  1.2000000000000000, 1.5500000000000000, 2.3999999999999999, 0.32000000000000001,
  0.22000000000000000, 2.6000000000000001, 4.2000000000000002, 190.00000000000000
};
inline constexpr char bowSourceHash0[] = "76a1bc70f855e58343a1bc1d7a41c6b72dcd2da55b3e4c47b5a5908756180999";
inline constexpr char bowSourceHash1[] = "da9340fa1ac6259cfcf1e54d9d5b03ca28b620d26b02e6ec87116fdcf0ca0137";
struct BladeSourceFixture {
  double statsRange, statsDamage, statsRate, statsMobility;
  double statsPaint, range, projSpeed, tapInterval;
  double tapInk, tapDrops, tapSpreadDeg, tapSpeed;
  double tapStraight, tapDamageNear, tapDamageFar, tapDropRadius;
  double tapMelee, chargeDelay, chargeTime, chargeStore;
  double heavyInk, heavyInterval, heavyMelee, lungeDist;
  double lungeTime, strokeRadius, waveDamage, waveDamageFar;
  double waveSpeed, waveRange, waveWidth, waveHeight;
  double wavePaintEvery, wavePaintRadius, waveEndRadius, reach;
  double meleeArcDeg, heavyArcDeg, moveSpeedFiring, moveSpeedCharging;
  double specialCost;
};
inline constexpr BladeSourceFixture bladeSourceFixture{
  0.34999999999999998, 0.84999999999999998, 0.75000000000000000, 0.84999999999999998,
  0.55000000000000004, 6.5000000000000000, 17.000000000000000, 0.27000000000000002,
  3.2000000000000002, 7.0000000000000000, 66.000000000000000, 17.500000000000000,
  0.070000000000000007, 40.000000000000000, 20.000000000000000, 0.64000000000000001,
  35.000000000000000, 0.14000000000000001, 0.62000000000000000, 1.0000000000000000,
  9.0000000000000000, 0.46000000000000002, 130.00000000000000, 1.0500000000000000,
  0.13000000000000000, 0.59999999999999998, 70.000000000000000, 50.000000000000000,
  26.000000000000000, 12.000000000000000, 0.69999999999999996, 1.7000000000000000,
  0.69999999999999996, 0.62000000000000000, 1.0500000000000000, 1.5000000000000000,
  150.00000000000000, 110.00000000000000, 5.5999999999999996, 4.4000000000000004,
  180.00000000000000
};
inline constexpr char bladeSourceHash0[] = "76a1bc70f855e58343a1bc1d7a41c6b72dcd2da55b3e4c47b5a5908756180999";
inline constexpr char bladeSourceHash1[] = "cccab9b54925601c6d45162c77ad2eb9b530dfca009956a2338441d0f1d08430";
struct MittsSourceFixture {
  double statsRange, statsDamage, statsRate, statsMobility;
  double statsPaint, range, punchInterval, inkPerPunch;
  double punchSpread, fistSpeed, fistRange, fistSize;
  double guardArmor, guardArc, guardAfter, botApproach;
  double punchDamage, splashRadius, splashMax, splashMin;
  double fistPaint, moveSpeedFiring, leapChargeTime, leapInkMin;
  double leapInkMax, moveSpeedCharging, leapArmor, leapArmorGrace;
  double leapSpeedMin, leapSpeedMax, leapAngle, leapAngleMin;
  double leapAngleMax, leapPitchK, leapVyMax, landRadius;
  double landDamageMax, landDamageMin, landPaint, landInvuln;
  double gloveRadius, gloveDamage, clingDrain, specialCost;
};
inline constexpr MittsSourceFixture mittsSourceFixture{
  0.29999999999999999, 0.75000000000000000, 0.90000000000000002, 0.90000000000000002,
  0.50000000000000000, 5.2999999999999998, 0.12000000000000000, 1.6000000000000001,
  2.5000000000000000, 28.000000000000000, 4.5999999999999996, 0.20000000000000001,
  0.50000000000000000, 70.000000000000000, 0.25000000000000000, 1.0000000000000000,
  38.000000000000000, 1.3999999999999999, 22.000000000000000, 10.000000000000000,
  0.94999999999999996, 5.4000000000000004, 0.45000000000000001, 9.0000000000000000,
  15.000000000000000, 1.1000000000000001, 0.34999999999999998, 0.14999999999999999,
  10.900000000000000, 18.600000000000001, 40.000000000000000, 18.000000000000000,
  60.000000000000000, 0.55000000000000004, 13.000000000000000, 2.7999999999999998,
  100.00000000000000, 35.000000000000000, 2.5000000000000000, 0.50000000000000000,
  1.1000000000000001, 40.000000000000000, 8.0000000000000000, 180.00000000000000
};
inline constexpr char mittsSourceHash0[] = "76a1bc70f855e58343a1bc1d7a41c6b72dcd2da55b3e4c47b5a5908756180999";
inline constexpr char mittsSourceHash1[] = "35178544efd3325b6f5d84c480d53876e0c73ae8e584d67cbf0513f3c0fdffad";
}
