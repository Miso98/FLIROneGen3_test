// -- define Flir calibration values ---------------
// exiftool -plan* FLIROne-2015-11-30-17-26-48+0100.jpg 

#if 0
#define  PlanckR1  16528.178
#define  PlanckB  1427.5
#define  PlanckF  1.0
#define  PlanckO  -1307.0
#define  PlanckR2  0.012258549

#define  TempReflected 20.0     // Reflected Apparent Temperature [°C]
#endif

// read from my camera, are these hardware depedent?
#define  PlanckR1  18417.0
#define  PlanckB  1435.0
#define  PlanckF  1.0
#define  PlanckO  -1656.0
#define  PlanckR2  0.0125

// Reflected Apparent Temperature [°C]
#define  TempReflected 22.0

// 0.01 to 0.99 on the emissivity scale.
// Highly polished metallic surfaces such as copper or aluminum usually have an emissivity below 0.10.
// Roughened or oxidized metallic surfaces will have a much higher emissivity
// (0.6 or greater depending on the surface condition and the amount of oxidation).
// Most flat-finish paints are around 0.90, while human skin and water are about 0.98.

// Emissivity of object
// defaults from app:
// matte		0.9
// half matte	0.8
// half shiny	0.6
// shiny		0.3
#define  Emissivity 0.90

