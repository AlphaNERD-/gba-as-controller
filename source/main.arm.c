#include <stdint.h>
#include <gba_input.h>
#include <gba_interrupt.h>
#include <gba_sio.h>
#include <gba_timers.h>
#include "bios.h"

#define ROM            ((uint8_t *)0x08000000)
#define ROM_GPIODATA *((uint16_t *)0x080000C4)
#define ROM_GPIODIR  *((uint16_t *)0x080000C6)
#define ROM_GPIOCNT  *((uint16_t *)0x080000C8)

#define MODIFIER_MASK ((uint8_t *)0x00FF)

enum {
	CMD_ID = 0x00,
	CMD_STATUS = 0x40,
	CMD_ORIGIN,
	CMD_RECALIBRATE,
	CMD_STATUS_LONG,
	CMD_RESET = 0xFF
};

enum {
	MOTOR_STOP = 0,
	MOTOR_RUMBLE,
	MOTOR_STOP_HARD
};

enum {
	INPUT_DIRECTION = 0x0,
	INPUT_BUTTON = 0x1,
	INPUT_MODIFIER = 0x2
};

enum {
	DIRECTION_LSTICK = 0x0,
	DIRECTION_CSTICK = 0x1,
	DIRECTION_DPAD = 0x2
};

enum {
	BUTTON_A = 0x01,
	BUTTON_B = 0x02,
	BUTTON_X = 0x03,
	BUTTON_Y = 0x04,
	BUTTON_L = 0x05,
	BUTTON_Z = 0x06,
	BUTTON_R = 0x07,
	BUTTON_START = 0x08
};

/*
 * Diese Struktur speichert die Eingaben vom GBA
 */
static struct {
	uint8_t Left : 1;
	uint8_t Right : 1;
	uint8_t Down : 1;
	uint8_t Up : 1;
	uint8_t A : 1;
	uint8_t B : 1;
	uint8_t L : 1;
	uint8_t R : 1;
	uint8_t Start : 1;
	uint8_t Select : 1;
} gbaButtons;

/*
 * Diese Klasse ist der Unterschied zwischen ExtremsCorner's "Anwendung"
 * und meiner göttlichen Kreation. Sie speichert die Tastenzuweisungen.
 *
 * Auf den ersten 4 Bit wird die Eingabe ohne Modifier gespeichert
 * Auf den letzten 4 Bit wird die Eingabe mit Modifier gespeichert
 *
 * In jedem 4 Bit Satz geben die ersten 2 Bit die Eingabeart an.
 * Die anderen 2 Bit stehen für die Eingabe selber.
 */
static struct {
	uint16_t DPad;
	uint16_t A;
	uint16_t B;
	uint16_t L;
	uint16_t R;
	uint16_t Start;
	uint8_t Select; //Select wird (vorerst) als Modifier fest programmiert. Das bedeutet, dass Select
} mapSettings;


/*
 * Diese Struktur speichert die Eingabe für den Gamecube
 */
static struct {
	uint8_t StickX;
	uint8_t StickY;
	uint8_t CStickX;
	uint8_t CStickY;
	uint8_t LPressure;
	uint8_t RPressure;
	uint8_t Left : 1;
	uint8_t Right : 1;
	uint8_t Down : 1;
	uint8_t Up : 1;
	uint8_t A : 1;
	uint8_t B : 1;
	uint8_t X : 1;
	uint8_t Y : 1;
	uint8_t L : 1;
	uint8_t Z : 1;
	uint8_t R : 1;
	uint8_t Start : 1;
} gcButtons;

/*
 * Liebe Julia. Spiel nicht an der Reihenfolge rum. Diese Reihenfolge braucht die
 * Gamecube. Und falls du dich fragst, wozu die Origin-Merker gut sind oder err_latch
 * und err_status... Das Internet weiß es auch nicht ganz. Behandeln wir es einfach wie Padding.
 * Die origin-Merker werden auf 1 gesetzt.
 */
struct buttons {
	uint16_t a          : 1;
	uint16_t b          : 1;
	uint16_t x          : 1;
	uint16_t y          : 1;
	uint16_t start      : 1;
	uint16_t get_origin : 1;
	uint16_t err_latch  : 1;
	uint16_t err_status : 1;
	uint16_t left       : 1;
	uint16_t right      : 1;
	uint16_t down       : 1;
	uint16_t up         : 1;
	uint16_t z          : 1;
	uint16_t r          : 1;
	uint16_t l          : 1;
	uint16_t use_origin : 1;
};

static struct {
	uint8_t type[2];

	struct {
		uint8_t mode   : 3;
		uint8_t motor  : 2;
		uint8_t origin : 1;
		uint8_t        : 2;
	} status;
} id;

/* 
 * In der Entwicklung geht es nicht um das "Warum?", sondern um das "Warum nicht?".
 * Warum muss ich die Variablen für die einzelnen Eingaben immer verschieden groß machen?
 * Na warum nicht? Ansonsten würde die Programmierung ja leichter werden.
 *
 * Danke Cave Johnson, jetzt übernehme ich. Unsere status-Struktur speichert die Eingsbe
 * die wir an die Gamecube schicken wollen. Warum gibt es mehrere Modi? Diese Frage
 * beantwortet ihnen wie immer gerne Cave Johnson.
 */
static struct {
	struct buttons buttons; //16 Bit
	struct { uint8_t x, y; } stick; //32 Bit
	union {
		struct {
			struct { uint8_t x : 8, y : 8; } substick;
			struct { uint8_t r : 4, l : 4; } trigger;
			struct { uint8_t b : 4, a : 4; } button;
		} mode0; //64 Bit

		struct {
			struct { uint8_t y : 4, x : 4; } substick;
			struct { uint8_t l : 8, r : 8; } trigger;
			struct { uint8_t b : 4, a : 4; } button;
		} mode1; //auch 64 Bit

		struct {
			struct { uint8_t y : 4, x : 4; } substick;
			struct { uint8_t r : 4, l : 4; } trigger;
			struct { uint8_t a : 8, b : 8; } button;
		} mode2; //wieder 64 Bit

		struct {
			struct { uint8_t x, y; } substick;
			struct { uint8_t l, r; } trigger;
		} mode3; //nochmal 64 Bit

		struct {
			struct { uint8_t x, y; } substick;
			struct { uint8_t a, b; } button;
		} mode4; //uuuuund 64 Bit
	};
} Response64;

/*
 * Die vorher als Response80 bekannte Origin-Klasse hat nur den Zweck
 * sich den Ausgangszustand des Controllers zu merken.
 */
static struct {
	struct buttons buttons;
	struct { uint8_t x, y; } stick;
	struct { uint8_t x, y; } substick;
	struct { uint8_t l, r; } trigger;
	struct { uint8_t a, b; } button;
} Origin = {
	.buttons  = { .use_origin = 1 },
	.stick    = { 128, 128 },
	.substick = { 128, 128 },
};

static uint8_t buffer[128];

static bool modifierActive = false;

/*
 * Es gibt Module mit Rumble. WAAAAAAAAAS?
 */
static bool has_motor(void)
{
	if (0x96 == ROM[0xB2]) {
		switch (ROM[0xAC]) {
			case 'R':
			case 'V':
				return true;
			default:
				return false;
		}
	}

	return false;
}

void SISetResponse(const void *buf, unsigned bits);
int SIGetCommand(void *buf, unsigned bits);

int IWRAM_CODE main(void) {
	RegisterRamReset(RESET_ALL_REG);

	REG_IE = IRQ_SERIAL | IRQ_TIMER2 | IRQ_TIMER1 | IRQ_TIMER0;
	REG_IF = REG_IF;

	REG_RCNT = R_GPIO | GPIO_IRQ | GPIO_SO_IO | GPIO_SO;

	REG_TM0CNT_L = -67;
	REG_TM1CNT_H = TIMER_START | TIMER_IRQ | TIMER_COUNT;
	REG_TM0CNT_H = TIMER_START;

	SoundBias(0);
	Halt();

	mapSettings.DPad = 0x0001;
	mapSettings.A = 0x1113;
	mapSettings.B = 0x1214;
	mapSettings.L = 0x1515;
	mapSettings.R = 0x1716;
	mapSettings.Start = 0x1818;
	mapSettings.Select = 0x20;

	while (true) {
		/* Hole Befehl von Gamecube ab */
		int length = SIGetCommand(buffer, sizeof(buffer) * 8 + 1);
		if (length < 9) continue;

		/* Frage Buttons ab. Die origin-Variable speichert die Eingabe zwischen. */
		unsigned buttons     = ~REG_KEYINPUT;
		gbaButtons.Right = !!(buttons & KEY_RIGHT);
		gbaButtons.Left = !!(buttons & KEY_LEFT);
		gbaButtons.Up = !!(buttons & KEY_UP);
		gbaButtons.Down = !!(buttons & KEY_DOWN);
		gbaButtons.A = !!(buttons & KEY_A);
		gbaButtons.B = !!(buttons & KEY_B);
		gbaButtons.L = !!(buttons & KEY_L);
		gbaButtons.R = !!(buttons & KEY_R);
		gbaButtons.Select = !!(buttons & KEY_SELECT);
		gbaButtons.Start = !!(buttons & KEY_START);

		ResetResponse();

		MapSelect(mapSettings.Select); //Zuerst verarbeiten, da Select als Modifier dient.
		MapDPad();

		if (gbaButtons.A != 0)
			MapButton(mapSettings.A);

		if (gbaButtons.B != 0)
			MapButton(mapSettings.B);

		if (gbaButtons.L != 0)
			MapButton(mapSettings.L);

		if (gbaButtons.R != 0)
			MapButton(mapSettings.R);

		if (gbaButtons.Start != 0)
			MapButton(mapSettings.Start);

		switch (buffer[0]) {
			case CMD_RESET:
				/* Vibration ausschalten */
				id.status.motor = MOTOR_STOP;
			case CMD_ID:
				if (length == 9) {
					/* Gamecube mitteilen, dass Vibration zur Verfügung steht */
					if (has_motor()) {
						id.type[0] = 0x09;
						id.type[1] = 0x00;
					} else {
						id.type[0] = 0x29;
						id.type[1] = 0x00;
					}

					SISetResponse(&id, sizeof(id) * 8);
				}
				break;
			case CMD_STATUS:
				if (length == 25) {
					id.status.mode  = buffer[1];
					id.status.motor = buffer[2];

					Response64.buttons.a = gcButtons.A;
					Response64.buttons.b = gcButtons.B;
					Response64.buttons.z = gcButtons.Z;
					Response64.buttons.start = gcButtons.Start;
					Response64.buttons.right = gcButtons.Right;
					Response64.buttons.left = gcButtons.Left;
					Response64.buttons.up = gcButtons.Up;
					Response64.buttons.down = gcButtons.Down;
					Response64.buttons.r = gcButtons.R;
					Response64.buttons.l = gcButtons.L;
					Response64.stick.x = gcButtons.StickX;
					Response64.stick.y = gcButtons.StickY;

					switch (id.status.mode) {
						default:
							Response64.mode0.substick.x = gcButtons.CStickX;
							Response64.mode0.substick.y = gcButtons.CStickY;
							Response64.mode0.trigger.l  = gcButtons.LPressure >> 4;
							Response64.mode0.trigger.r  = gcButtons.RPressure >> 4;
							Response64.mode0.button.a   = gcButtons.A >> 4;
							Response64.mode0.button.b   = gcButtons.B >> 4;
							break;
						case 1:
							Response64.mode1.substick.x = gcButtons.CStickX >> 4;
							Response64.mode1.substick.y = gcButtons.CStickY >> 4;
							Response64.mode1.trigger.l  = gcButtons.LPressure;
							Response64.mode1.trigger.r  = gcButtons.RPressure;
							Response64.mode1.button.a   = gcButtons.A >> 4;
							Response64.mode1.button.b   = gcButtons.B >> 4;
							break;
						case 2:
							Response64.mode2.substick.x = gcButtons.CStickX >> 4;
							Response64.mode2.substick.y = gcButtons.CStickY >> 4;
							Response64.mode2.trigger.l  = gcButtons.LPressure >> 4;
							Response64.mode2.trigger.r  = gcButtons.RPressure >> 4;
							Response64.mode2.button.a   = gcButtons.A;
							Response64.mode2.button.b   = gcButtons.B;
							break;
						case 3:
							Response64.mode3.substick.x = gcButtons.CStickX;
							Response64.mode3.substick.y = gcButtons.CStickY;
							Response64.mode3.trigger.l  = gcButtons.LPressure;
							Response64.mode3.trigger.r  = gcButtons.RPressure;
							break;
						case 4:
							Response64.mode4.substick.x = gcButtons.CStickX;
							Response64.mode4.substick.y = gcButtons.CStickY;
							Response64.mode4.button.a   = gcButtons.A;
							Response64.mode4.button.b   = gcButtons.B;
							break;
					}

					SISetResponse(&Response64, sizeof(Response64) * 8);
				}
				break;
			case CMD_ORIGIN:
				Origin.buttons.a = gcButtons.A;
				Origin.buttons.b = gcButtons.B;
				Origin.buttons.z = gcButtons.Z;
				Origin.buttons.start = gcButtons.Start;
				Origin.buttons.right = gcButtons.Right;
				Origin.buttons.left = gcButtons.Left;
				Origin.buttons.up = gcButtons.Up;
				Origin.buttons.down = gcButtons.Down;
				Origin.buttons.r = gcButtons.R;
				Origin.buttons.l = gcButtons.L;

				if (length == 9) 
					SISetResponse(&Origin, sizeof(Origin) * 8);

				break;
			case CMD_RECALIBRATE:
			case CMD_STATUS_LONG:
				if (length == 25) {
					id.status.mode  = buffer[1];
					id.status.motor = buffer[2];

					SISetResponse(&Origin, sizeof(Origin) * 8);
				}
				break;
		}

		switch (id.status.motor) {
			default:
				ROM_GPIODATA = 0;
				break;
			case MOTOR_RUMBLE:
				ROM_GPIOCNT  = 1;
				ROM_GPIODIR  = 1 << 3;
				ROM_GPIODATA = 1 << 3;
				break;
		}
	}
}

void ResetResponse() {
	gcButtons.A = Origin.buttons.a;
	gcButtons.B = Origin.buttons.b;
	gcButtons.X = Origin.buttons.x;
	gcButtons.Y = Origin.buttons.y;
	gcButtons.Z = Origin.buttons.z;
	gcButtons.Start = Origin.buttons.start;
	gcButtons.Right = Origin.buttons.right;
	gcButtons.Left = Origin.buttons.left;
	gcButtons.Up = Origin.buttons.up;
	gcButtons.Down = Origin.buttons.down;
	gcButtons.R = Origin.buttons.r;
	gcButtons.L = Origin.buttons.l;
	gcButtons.RPressure = Origin.trigger.r;
	gcButtons.LPressure = Origin.trigger.l;
	gcButtons.StickX = Origin.stick.x;
	gcButtons.StickY = Origin.stick.y;
	gcButtons.CStickX = Origin.substick.x;
	gcButtons.CStickY = Origin.substick.y;
}

void MapSelect(uint8_t mapCommand) {
	if ((mapCommand & 0xF0) == INPUT_MODIFIER)
		if (gbaButtons.Select != 0)
			modifierActive = true;
		else
			modifierActive = false;
	else if ((mapCommand & 0xF) == INPUT_BUTTON)
		MapButton(gbaButtons.Select, mapSettings.Select);
}

void MapButton(uint16_t mapperLine) {
	uint8_t mapCommand = modifierActive ? (mapperLine & 0x00FF) : ((mapperLine & 0xFF00) >> 8);

	if ((mapCommand & 0xF0) == INPUT_BUTTON)
	{
		if ((mapCommand & 0x0F) == BUTTON_A)
			gcButtons.A = 1;

		if ((mapCommand & 0x0F) == BUTTON_B)
			gcButtons.B = 1;

		if ((mapCommand & 0x0F) == BUTTON_X)
			gcButtons.X = 1;

		if ((mapCommand & 0x0F) == BUTTON_Y)
			gcButtons.Y = 1;

		if ((mapCommand & 0x0F) == BUTTON_Z)
			gcButtons.Z = 1;

		if ((mapCommand & 0x0F) == BUTTON_L)
		{
			gcButtons.L = 1;
			gcButtons.LPressure = 200;
		}

		if ((mapCommand & 0x0F) == BUTTON_R)
		{
			gcButtons.R = 1;
			gcButtons.RPressure = 200;
		}

		if ((mapCommand & 0x0F) == BUTTON_START)
			gcButtons.Start = 1;
	}
}

void MapDPad(uint8_t mapCommand) {
	if ((mapCommand & 0xF) == INPUT_DIRECTION)
	{
		if ((mapCommand & 0x0F) == DIRECTION_LSTICK)
		{
			if (gbaButtons.Right != 0)
				gcButtons.StickX = 255;
			else if (gbaButtons.Left != 0)
				gcButtons.StickX = 0;

			if (gbaButtons.Up != 0)
				gcButtons.StickY = 255;
			else if (gbaButtons.Down != 0)
				gcButtons.StickY = 0;
		}

		if ((mapCommand & 0x0F) == DIRECTION_CSTICK)
		{
			if (gbaButtons.Right != 0)
				gcButtons.CStickX = 255;
			else if (gbaButtons.Left != 0)
				gcButtons.CStickX = 0;

			if (gbaButtons.Up != 0)
				gcButtons.CStickY = 255;
			else if (gbaButtons.Down != 0)
				gcButtons.CStickY = 0;
		}

		if ((mapCommand & 0x0F) == DIRECTION_DPAD)
		{
			if (gbaButtons.Right != 0)
				gcButtons.Right = 1;

			if (gbaButtons.Left != 0)
				gcButtons.Left = 1;

			if (gbaButtons.Up != 0)
				gcButtons.Up = 1;

			if (gbaButtons.Down != 0)
				gcButtons.Down = 1;
		}
	}
}