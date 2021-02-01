// -- define Flir calibration values ---------------
// exiftool -plan* FLIROne-2015-11-30-17-26-48+0100.jpg 

#define  PlanckR1  16528.178
#define  PlanckB  1427.5
#define  PlanckF  1.0
#define  PlanckO  -1307.0
#define  PlanckR2  0.012258549

#define  TempReflected 20.0     // Reflected Apparent Temperature [°C]

// 0.01 to 0.99 on the emissivity scale.
// Highly polished metallic surfaces such as copper or aluminum usually have an emissivity below 0.10.
// Roughened or oxidized metallic surfaces will have a much higher emissivity
// (0.6 or greater depending on the surface condition and the amount of oxidation).
// Most flat-finish paints are around 0.90, while human skin and water are about 0.98.

#define  Emissivity 0.95  // Emissivity of object
