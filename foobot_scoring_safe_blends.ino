#include <Adafruit_NeoPixel.h>

// Timestep config
#define TIMESTEP 1

// NeoPixel config

#define LED_PIN 4
#define LED_COUNT 6

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// Blend buffers
enum EBuffer
{
  ScoreBuffer = 0,
  CurrentFXBuffer,
  LastFXBuffer,
  BufferCount,
};

byte blend_buffers[BufferCount][LED_COUNT][3] = {0};

byte score_blend = 0x0;
byte current_fx_blend = 0xff;

// Game and LED config

const byte num_teams = 2;
const byte num_leds_per_team = 3;

enum States
{
  STATE_OFF = 0,
  STATE_ON,
  NUM_STATES,
};

byte scores[num_teams] = {0, 0};

byte led_ids[num_teams][num_leds_per_team] = {
  {0, 1, 2},
  {3, 4, 5},
};

byte state_colours[num_teams][NUM_STATES][3] = {
  {
    {0xff, 0x00, 0x00},
    {0x00, 0xff, 0x00},
  },
  {
    {0x00, 0x00, 0xff},
    {0x00, 0xff, 0x00},
  },
};

void set_led(byte team, byte led, byte state)
{
  blend_buffers[ScoreBuffer][led_ids[team][led]][0] = state_colours[team][state][0];
  blend_buffers[ScoreBuffer][led_ids[team][led]][1] = state_colours[team][state][1];
  blend_buffers[ScoreBuffer][led_ids[team][led]][2] = state_colours[team][state][2];
}

// Interrupt to game communication
byte i_flags = 0;

const byte IF_THROWIN = 0x80;
const byte IF_SCOREMASK = 0x7f;
const byte IF_SCORED = 0x01;

// Pin change interrupt handler
const byte throwin_pin = 6;
const byte score0_pin = 5;
const byte score1_pin = 7;

ISR(PCINT2_vect)  // D0 to D7
{
  if (!digitalRead(throwin_pin))
  {
    i_flags |= IF_THROWIN;
  }
  if (!digitalRead(score0_pin))
  {
    i_flags = IF_SCORED << 0;
  }
  if (!digitalRead(score1_pin))
  {
    i_flags = IF_SCORED << 1;
  }
}

void pciSetup(byte pin)
{
  *digitalPinToPCMSK(pin) |= bit(digitalPinToPCMSKbit(pin)); // Enable pin
  PCIFR |= bit(digitalPinToPCICRbit(pin)); // Clear existing interrupt
  PCICR |= bit(digitalPinToPCICRbit(pin)); // Enable interrupt for the group
}

void setup()
{
  Serial.begin(9600);

  pinMode(throwin_pin, INPUT_PULLUP);
  pinMode(score0_pin, INPUT_PULLUP);
  pinMode(score1_pin, INPUT_PULLUP);
  pciSetup(throwin_pin);
  pciSetup(score0_pin);
  pciSetup(score1_pin);
  
  strip.begin();
  display_scores();
  strip.show();
}

// Game state machine
enum GameState
{
  GS_WAITING = 0,
  GS_PLAYING,
  GS_GAME_OVER,
};

GameState gamestate = GS_WAITING;

// Transient states, for effects
enum TransientState
{
  TS_NORMAL = 0,
  TS_ATTRACT,
  TS_KICKOFF,
  TS_GOAL0,
  TS_GOAL1,
  TS_WINNER,
};

TransientState transientstate = TS_ATTRACT;
uint16_t fx_count = 0;
uint16_t fx_ms = 0;
uint16_t fx_lasttick = 0;
byte fx_dir = 1;
byte fx_pos = 0;

// Prints a rainbow optimised for a 96 LED strip
// The rainbow begins at a specified position. 
// ROY G BIV!
byte winner0_map[] = {2, 1, 0, 3, 4, 5};
byte winner1_map[] = {5, 3, 3, 0, 1, 2};

void rainbow96(byte startPosition, const byte* mapping)
{
  // Next we setup each pixel with the right color
  for (int i=0; i<LED_COUNT * 16; i += 16)
  {
    // There are 96 total colors we can get out of the rainbowOrder function.
    // It'll return a color between red->orange->green->...->violet for 0-95.
    uint32_t colour = rainbowOrder96((i + startPosition) % 96);
    blend_buffers[CurrentFXBuffer][mapping[LED_COUNT - (i/16) - 1]][0] = (colour & 0xff);
    blend_buffers[CurrentFXBuffer][mapping[LED_COUNT - (i/16) - 1]][1] = (colour >> 8) & 0xff;
    blend_buffers[CurrentFXBuffer][mapping[LED_COUNT - (i/16) - 1]][2] = (colour >> 16) & 0xff;
  }
}

// Input a value 0 to 95 to get a color value.
// The colors are a transition red->yellow->green->aqua->blue->fuchsia->red...
//  Adapted from Wheel function in the Adafruit_NeoPixel library example sketch
uint32_t rainbowOrder96(byte position) 
{
  const byte section = position / 16;
  const byte parameter = (position % 16) * 16;
  // 6 total zones of color change:
  switch (section)
  {
    case 0: return strip.Color(0xFF, parameter, 0);        // Red -> Yellow (Red = FF, blue = 0, green goes 00-FF)
    case 1: return strip.Color(0xFF - parameter, 0xFF, 0); // Yellow -> Green (Green = FF, blue = 0, red goes FF->00)
    case 2: return strip.Color(0, 0xFF, parameter);        // Green->Aqua (Green = FF, red = 0, blue goes 00->FF)
    case 3: return strip.Color(0, 0xFF - parameter, 0xFF); // Aqua->Blue (Blue = FF, red = 0, green goes FF->00)
    case 4: return strip.Color(parameter, 0, 0xFF);        // Blue->Fuchsia (Blue = FF, green = 0, red goes 00->FF)
    case 5: return strip.Color(0xFF, 0, 0xFF - parameter); // Fuchsia->Red (Red = FF, green = 0, blue goes FF->00)
    default: return strip.Color(0, 0, 0);                  // Turn off for debug
  }
}

void display_scores()
{
  for (byte team = 0; team < num_teams; ++team)
  {
    for (byte led = 0; led < num_leds_per_team; ++led)
    {
      byte blink = (transientstate == TS_GOAL0 + team) ? (((fx_ms / 200) + 1) % 2) : 1;
      byte state = ((scores[team] > led) && blink) ? STATE_ON : STATE_OFF;
      set_led(team, led, state);
    }
  }
}

void blend_to_new_fx()
{
  // Copy current FX buffer to last FX buffer and start blending to the new effect
  current_fx_blend = 0x00;
  for (int led = 0; led < LED_COUNT; ++led)
  {
    blend_buffers[LastFXBuffer][led][0] = blend_buffers[CurrentFXBuffer][led][0];
    blend_buffers[LastFXBuffer][led][1] = blend_buffers[CurrentFXBuffer][led][1];
    blend_buffers[LastFXBuffer][led][2] = blend_buffers[CurrentFXBuffer][led][2];
  }
}

void ts_set(byte state)
{
  // Set transient state
  transientstate = state;

  // Reset transient state counters
  fx_count = 0;
  fx_ms = 0;
  fx_lasttick = 0;
  fx_pos = 0;
  fx_dir = 1;
}

void throwin()
{
  if (gamestate == GS_WAITING)
  {
    gamestate = GS_PLAYING;
    ts_set(TS_KICKOFF);
  }
  else if (gamestate == GS_GAME_OVER)
  {
    for (byte team = 0; team < num_teams; ++team)
    {
      scores[team] = 0;
    }
    gamestate = GS_PLAYING;
    ts_set(TS_KICKOFF);
  }
}

void score(byte team)
{
  if (gamestate == GS_PLAYING)
  {
    scores[team]++;

    if (scores[team] >= num_leds_per_team)
    {
      gamestate = GS_GAME_OVER;
      ts_set(TS_GOAL0 + team);
    }
    else
    {
      gamestate = GS_WAITING;
      ts_set(TS_GOAL0 + team);
    }
  }
}

void loop()
{
  // Snapshot the flags from the interrupt handler for processing
  noInterrupts();
  byte p_flags = i_flags;
  i_flags = 0;
  interrupts();

  // Process state changes
  if (p_flags & IF_THROWIN)
  {
    throwin();
  }
  for (byte team = 0; team < num_teams; ++team)
  {
    if (p_flags & (IF_SCORED << team))
    {
      score(team);
    }
  }
  
  // Process game state machine
  switch (gamestate)
  {
    case GS_WAITING:
    {
      
    } break;
    case GS_PLAYING:
    {
      
    } break;
    case GS_GAME_OVER:
    {
      
    } break;
  }

  // Display scores unless overwritten by effects
  display_scores();

  // Process transient effects state machine
  switch (transientstate)
  {
    case TS_NORMAL: break;
    case TS_ATTRACT:
    {
      // Fast scanning ping pong effect
      const byte attract_map[] = {2, 1, 0, 3, 4, 5};
      rainbow96(fx_pos, attract_map);
      if (fx_ms - fx_lasttick > 10)
      {
        fx_pos += fx_dir;
        if (fx_pos <= 0)
        {
          fx_dir = 1;
        }
        if (fx_pos >= 96)
        {
          fx_dir = -1;
        }
        fx_lasttick = fx_ms;
      }
    } break;
    case TS_KICKOFF:
    {
      // First wait for blend to FX and record the time
      if (fx_pos == 0 && score_blend == 0)
      {
        fx_pos = 1;
        fx_lasttick = fx_ms;
      }

      // Copy pixels into the effects buffer from the score buffer for wipe effect
      //   start counting from when the effect fully blended in
      if (!fx_pos || fx_ms - fx_lasttick < 150)
      {
        blend_buffers[CurrentFXBuffer][0][0] = 0x00;
        blend_buffers[CurrentFXBuffer][0][1] = 0x00;
        blend_buffers[CurrentFXBuffer][0][2] = 0xff;
        blend_buffers[CurrentFXBuffer][3][0] = 0xff;
        blend_buffers[CurrentFXBuffer][3][1] = 0x00;
        blend_buffers[CurrentFXBuffer][3][2] = 0x00;
      }
      else
      {
        blend_buffers[CurrentFXBuffer][0][0] = blend_buffers[ScoreBuffer][0][0];
        blend_buffers[CurrentFXBuffer][0][1] = blend_buffers[ScoreBuffer][0][1];
        blend_buffers[CurrentFXBuffer][0][2] = blend_buffers[ScoreBuffer][0][2];
        blend_buffers[CurrentFXBuffer][3][0] = blend_buffers[ScoreBuffer][3][0];
        blend_buffers[CurrentFXBuffer][3][1] = blend_buffers[ScoreBuffer][3][1];
        blend_buffers[CurrentFXBuffer][3][2] = blend_buffers[ScoreBuffer][3][2];
      }
      
      if (!fx_pos || fx_ms - fx_lasttick < 250)
      {
        blend_buffers[CurrentFXBuffer][1][0] = 0x00;
        blend_buffers[CurrentFXBuffer][1][1] = 0x00;
        blend_buffers[CurrentFXBuffer][1][2] = 0xff;
        blend_buffers[CurrentFXBuffer][4][0] = 0xff;
        blend_buffers[CurrentFXBuffer][4][1] = 0x00;
        blend_buffers[CurrentFXBuffer][4][2] = 0x00;
      }
      else
      {
        blend_buffers[CurrentFXBuffer][1][0] = blend_buffers[ScoreBuffer][1][0];
        blend_buffers[CurrentFXBuffer][1][1] = blend_buffers[ScoreBuffer][1][1];
        blend_buffers[CurrentFXBuffer][1][2] = blend_buffers[ScoreBuffer][1][2];
        blend_buffers[CurrentFXBuffer][4][0] = blend_buffers[ScoreBuffer][4][0];
        blend_buffers[CurrentFXBuffer][4][1] = blend_buffers[ScoreBuffer][4][1];
        blend_buffers[CurrentFXBuffer][4][2] = blend_buffers[ScoreBuffer][4][2];
      }
      
      if (!fx_pos || fx_ms - fx_lasttick < 350)
      {
        blend_buffers[CurrentFXBuffer][2][0] = 0x00;
        blend_buffers[CurrentFXBuffer][2][1] = 0x00;
        blend_buffers[CurrentFXBuffer][2][2] = 0xff;
        blend_buffers[CurrentFXBuffer][5][0] = 0xff;
        blend_buffers[CurrentFXBuffer][5][1] = 0x00;
        blend_buffers[CurrentFXBuffer][5][2] = 0x00;
      }
      else
      {
        blend_buffers[CurrentFXBuffer][2][0] = blend_buffers[ScoreBuffer][2][0];
        blend_buffers[CurrentFXBuffer][2][1] = blend_buffers[ScoreBuffer][2][1];
        blend_buffers[CurrentFXBuffer][2][2] = blend_buffers[ScoreBuffer][2][2];
        blend_buffers[CurrentFXBuffer][5][0] = blend_buffers[ScoreBuffer][5][0];
        blend_buffers[CurrentFXBuffer][5][1] = blend_buffers[ScoreBuffer][5][1];
        blend_buffers[CurrentFXBuffer][5][2] = blend_buffers[ScoreBuffer][5][2];
        ts_set(TS_NORMAL);
      }
    } break;
    case TS_GOAL0:
    case TS_GOAL1:
    {
      if (fx_ms > 1000)
      {
        ts_set((gamestate == GS_GAME_OVER) ? TS_WINNER : TS_NORMAL);
      }
    } break;
    case TS_WINNER:
    {
      // Fast scanning ping pong effect
      const byte attract_map[] = {2, 1, 0, 3, 4, 5};
      rainbow96(fx_pos, attract_map);
      if (fx_ms - fx_lasttick > 3)
      {
        if (scores[0] > scores[1])
        {
          fx_pos -= 1;
          if (fx_pos <= 0)
          {
            fx_pos = 95;
          }
        }
        else
        {
          fx_pos += 1;
          if (fx_pos >= 96)
          {
            fx_pos = 0;
          }
        }
        fx_lasttick = fx_ms;
      }
      if (fx_ms > 3000)
      {
        // Blended transition back to attract mode
        ts_set(TS_ATTRACT);
        blend_to_new_fx();
      }
    } break;
  }

  // Update blend values
  // Score or FX
  switch (transientstate)
  {
    case TS_NORMAL:
    case TS_GOAL0:
    case TS_GOAL1:
    {
      // Blend rapidly to score
      const byte blend_speed = 16;
      if (score_blend < 0xff - blend_speed)
      {
        score_blend += 16;
      }
      else
      {
        score_blend = 0xff;
      }
    } break;
    default:
    {
      // Blend to effects
      const byte blend_speed = 1;
      if (score_blend > blend_speed)
      {
        score_blend -= blend_speed;
      }
      else
      {
        score_blend = 0;
      }
    } break;
  }
  // Blend in new FX
  {
    const byte blend_speed = 8;
    if (current_fx_blend < 0xff - blend_speed)
    {
      current_fx_blend += 1;
    }
    else
    {
      current_fx_blend = 0xff;
    }
  }

  // Slow debug prints
  if (0)
  {
    Serial.print("GS: ");
    Serial.print(gamestate);
    Serial.print(" TS: ");
    Serial.println(transientstate);
    Serial.print(" Blends: score: ");
    Serial.print(score_blend);
    Serial.print(" current fx: ");
    Serial.println(current_fx_blend);
  }

  // Process blend tree
  byte front_buffer[LED_COUNT][3] = {0};
  byte fx_buffer[LED_COUNT][3] = {0};

  // Need to blend FX?
  if (score_blend < 0xff)
  {
    // Blend old to current FX
    for (int led = 0; led < LED_COUNT; ++led)
    {
      const byte blend = current_fx_blend;
      const byte blend_i = current_fx_blend ^ 0xff;
      fx_buffer[led][0] = (blend_buffers[CurrentFXBuffer][led][0] & blend) | (blend_buffers[LastFXBuffer][led][0] & blend_i);
      fx_buffer[led][1] = (blend_buffers[CurrentFXBuffer][led][1] & blend) | (blend_buffers[LastFXBuffer][led][1] & blend_i);
      fx_buffer[led][2] = (blend_buffers[CurrentFXBuffer][led][2] & blend) | (blend_buffers[LastFXBuffer][led][2] & blend_i);
    }
  }
  // Blend FX and scores
  for (int led = 0; led < LED_COUNT; ++led)
  {
    const byte blend = score_blend;
    const byte blend_i = score_blend ^ 0xff;
    front_buffer[led][0] = (blend_buffers[ScoreBuffer][led][0] & blend) | (fx_buffer[led][0] & blend_i);
    front_buffer[led][1] = (blend_buffers[ScoreBuffer][led][1] & blend) | (fx_buffer[led][1] & blend_i);
    front_buffer[led][2] = (blend_buffers[ScoreBuffer][led][2] & blend) | (fx_buffer[led][2] & blend_i);
  }

  // Update the strip
  for (int led = 0; led < LED_COUNT; ++led)
  {
    strip.setPixelColor(led, front_buffer[led][0], front_buffer[led][1], front_buffer[led][2]);
  }
  strip.show();

  // Update effects counters
  ++fx_count;
  fx_ms = fx_count * TIMESTEP;

  // Wait a timestep
  delay(TIMESTEP);
}
