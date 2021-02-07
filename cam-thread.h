#ifndef _CAM_THREAD_H
#define _CAM_THREAD_H

enum ffcstate_t {FFC_VALID_RAD, FFC_RAD_APPRO, FFC_PROGRESS, FFC_UNKNOWN};
enum shutterstate_t {sON, sFFC, sUNKNOWN};

// {
//   "shutterState":"ON",
//   "shutterTemperature":310.679992675781,
//   "usbNotifiedTimestamp":1184542349.84666,
//   "usbEnqueuedTimestamp":1184542349.85135,
//   "ffcState":"FFC_VALID_RAD"
// }
struct shutter_state_t {
	enum shutterstate_t shutterState;			// ON or FFC
	double shutterTemperature;		// in Kelvin? C = Kelvin - 273.15
	double usbNotifiedTimestamp;
	double usbEnqueuedTimestamp;
	enum ffcstate_t ffcState;		// FFC_VALID_RAD or FFC_PROGRESS
};

enum chargingState_t {stateNoCharging, stateCharging, stateUNKNOWN};

struct battery_state_t {
	enum chargingState_t chargingState;
	double voltage;
	int percentage;
	double chargingCurrent;
};


struct t_data_t {
	unsigned short *raw_ir_buffer;
	unsigned char *jpeg_buffer;
	unsigned int jpeg_size;
	unsigned char *ir_buffer;
	double emissivity;
	double tempreflected;
	double t_min, t_max, t_center;
	struct shutter_state_t shutter_state;
	struct battery_state_t battery_state;
	gboolean flir_run;
	unsigned char *color_palette;
};

#endif
