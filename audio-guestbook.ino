/**
 * Audio Guestbook, Copyright (c) 2022 Playful Technology
 * 
 * Tested using a Teensy 4.0 with Teensy Audio Shield, although should work 
 * with minor modifications on other similar hardware
 * 
 * When handset is lifted, a pre-recorded greeting message is played, followed by a tone.
 * Then, recording starts, and continues until the handset is replaced.
 * Playback button allows all messages currently saved on SD card through earpiece 
 * 
 * Files are saved on SD card as 44.1kHz, 16-bit, mono signed integer RAW audio format 
 * --> changed this to WAV recording, DD4WH 2022_07_31
 * --> added MTP support, which enables copying WAV files from the SD card via the USB connection, DD4WH 2022_08_01
 * 
 * 
 * Frank DD4WH, August 1st 2022 
 * for a DBP 611 telephone (closed contact when handheld is lifted) & with recording to WAV file
 * contact for switch button 0 is closed when handheld is lifted
 * 
 * GNU GPL v3.0 license
 * 
 */

//#define AUDIO_BLOCK_SAMPLES 160

#include <Bounce.h>
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <TimeLib.h>
#include <MTP_Teensy.h>
#include "play_sd_wav.h" // local copy with fixes

// DEFINES
// Define pins used by Teensy Audio Shield
#define SDCARD_CS_PIN    10
#define SDCARD_MOSI_PIN  11
#define SDCARD_SCK_PIN   13
// And those used for inputs
#define HOOK_PIN 0
#define PLAYBACK_BUTTON_PIN 1

#define VOLUME_PIN 41

#define noINSTRUMENT_SD_WRITE


// GLOBALS


#include <SerialFlash.h>

// GUItool: begin automatically generated code
AudioInputI2S            i2sIn;           //xy=388,136
AudioRecordQueue         queue1;          //xy=545,134
AudioSynthWaveform       waveform1;       //xy=558,187
AudioPlaySdWav           playWav1;        //xy=568,235
AudioMixer4              mixerLeft;       //xy=742,160
AudioMixer4              mixerRight;      //xy=742,260
AudioOutputI2S           i2sOut;          //xy=929,215
AudioConnection          patchCord1(i2sIn, 0, queue1, 0);
AudioConnection          patchCord2(waveform1, 0, mixerLeft, 0);
AudioConnection          patchCord3(waveform1, 0, mixerRight, 0);
AudioConnection          patchCord4(playWav1, 0, mixerLeft, 1);
AudioConnection          patchCord5(playWav1, 1, mixerRight, 1);
AudioConnection          patchCord6(mixerLeft, 0, i2sOut, 0);  // Left channel
AudioConnection          patchCord7(mixerRight, 0, i2sOut, 1); // Right channel
AudioControlSGTL5000     sgtl5000_1;      //xy=555,487



// Filename to save audio recording on SD card
char filename[15];
// The file object itself
File frec;

// Use long 40ms debounce time on both switches
Bounce buttonRecord = Bounce(HOOK_PIN, 120);
Bounce buttonPlay = Bounce(PLAYBACK_BUTTON_PIN, 120);

// Keep track of current state of the device
enum Mode {Initialising, Ready, Prompting, Recording, Playing};
Mode mode = Mode::Initialising;

float beep_volume = 0.7f; // not too loud :-)

uint32_t MTPcheckInterval; // default value of device check interval [ms]

// variables for writing to WAV file
unsigned long ChunkSize = 0L;
const unsigned long Subchunk1Size = 16;
const unsigned int AudioFormat = 1;
const unsigned int numChannels = 1;
const unsigned long sampleRate = 44100;
const unsigned int bitsPerSample = 16;
const unsigned long byteRate = sampleRate*numChannels*(bitsPerSample/8);// samplerate x channels x (bitspersample / 8)
const unsigned int blockAlign = numChannels*bitsPerSample/8;
unsigned long Subchunk2Size = 0L;
unsigned long recByteSaved = 0L;
unsigned long NumSamples = 0L;
byte byte1, byte2, byte3, byte4;

const float maxVolume = 0.7f;

unsigned long previousMillis = 0;  // Proměnná pro uchování času posledního čtení
const long interval = 500;         // Interval mezi čteními (250 milisekund)
const int numReadings = 10;
int readings[numReadings];
int readIndex = 0;                    // Index pro ukládání nového čtení
float total = 0;                      // Součet pro průměr
float average = 0;                    // Průměrná hodnota


void zeroReadings() {
  for (int i = 0; i < numReadings; i++) {
    readings[i] = 0;                  // Inicializujeme pole hodnot na 0
  }
}


void setup() {
  Serial.begin(9600);
  delay(250);

  zeroReadings();
  
//  while (!Serial && millis() < 5000) {
//    // wait for serial port to connect.
//  }
  Serial.println("Serial set up correctly");
  Serial.print("Buffer: ");
  Serial.println(AUDIO_BLOCK_SAMPLES);
  Serial.printf("Audio block set to %d samples\n",AUDIO_BLOCK_SAMPLES);
  print_mode();
  // Configure the input pins
  pinMode(HOOK_PIN, INPUT_PULLUP);
  pinMode(PLAYBACK_BUTTON_PIN, INPUT_PULLUP);

  // Audio connections require memory, and the record queue
  // uses this memory to buffer incoming audio.
  AudioMemory(128);

  // Enable the audio shield, select input, and enable output
  sgtl5000_1.enable();
  sgtl5000_1.inputSelect(AUDIO_INPUT_MIC);
  sgtl5000_1.volume(maxVolume);

  mixerLeft.gain(0, 1.0);  // Left channel gain for waveform1
  mixerLeft.gain(1, 1.0);  // Left channel gain for playWav1

  mixerRight.gain(0, 0.5); // Right channel gain for waveform1
  mixerRight.gain(1, 0.5); // Right channel gain for playWav1

  // Play a beep to indicate system is online
  Serial.println("Starting beep");
  waveform1.begin(beep_volume, 440, WAVEFORM_SINE);
  delay(100);
  waveform1.begin(beep_volume, 880, WAVEFORM_SINE);
  delay(100);
  waveform1.begin(beep_volume, 440, WAVEFORM_SINE);
  delay(100);  
  waveform1.amplitude(0);
  delay(100);
  Serial.println("Beep ended");

  // Initialize the SD card
  SPI.setMOSI(SDCARD_MOSI_PIN);
  SPI.setSCK(SDCARD_SCK_PIN);
  if (!(SD.begin(SDCARD_CS_PIN))) 
  {
    // stop here if no SD card, but print a message
    while (1) {
      Serial.println("Unable to access the SD card");
      delay(1000);
    }
  }
  else Serial.println("SD card correctly initialized");

  MTP.begin();
  MTP.addFilesystem(SD, "Kais Audio guestbook");
  Serial.println("Added SD card via MTP");
  MTPcheckInterval = MTP.storage()->get_DeltaDeviceCheckTimeMS();
  
  sgtl5000_1.micGain(1);
  setSyncProvider(getTeensy3Time);
  FsDateTime::setCallback(dateTime);

  mode = Mode::Ready; print_mode();

    // Play a beep to indicate system is online
  Serial.println("Starting beep");
  waveform1.begin(beep_volume, 880, WAVEFORM_SINE);
  delay(100);
  waveform1.begin(beep_volume, 1600, WAVEFORM_SINE);
  delay(100);
  waveform1.begin(beep_volume, 880, WAVEFORM_SINE);
  delay(100);  
  waveform1.amplitude(0);
  delay(100);
  Serial.println("Beep ended");
}

void updateVolume() {
    int val = analogRead(VOLUME_PIN);
    
    
    total = total - readings[readIndex];
    readings[readIndex] = val;
    total = total + readings[readIndex];
    readIndex = (readIndex + 1) % numReadings;
    

    average = total / numReadings;
    
    float mappedValue = map(average, 0, 1024, 0.05, maxVolume);
    
    Serial.print("Volume Average: ");
    Serial.println(mappedValue);
    sgtl5000_1.volume(mappedValue);
}

void loop() {
  // First, read the buttons
  buttonRecord.update();
  buttonPlay.update();

  switch(mode){
    case Mode::Ready:
      // Falling edge occurs when the handset is lifted --> 611 telephone
      if (buttonRecord.fallingEdge()) {
        Serial.println("Handset lifted");
        mode = Mode::Prompting; print_mode();
      }
      else if(buttonPlay.fallingEdge()) {
        //playAllRecordings();
        playLastRecording();
      }
      break;

    case Mode::Prompting:
      // Wait a second for users to put the handset to their ear
      wait(1000);
      // Play the greeting inviting them to record their message
      Serial.println("Play greeting:");
      playWav1.play("greeting.wav");    
      // Wait until the  message has finished playing
//      while (playWav1.isPlaying()) {
      while (!playWav1.isStopped()) {
        // Check whether the handset is replaced
        Serial.print(".");
        wait(5);
        buttonRecord.update();
        buttonPlay.update();
        // Handset is replaced
        if(buttonRecord.risingEdge()) {
          playWav1.stop();
          mode = Mode::Ready; print_mode();
          return;
        }
        if(buttonPlay.fallingEdge()) {
          playWav1.stop();
          //playAllRecordings();
          playLastRecording();
          return;
        }
        
      }
      
      // Debug message
      Serial.println("Starting Recording");
      // Play the tone sound effect
      waveform1.begin(beep_volume, 440, WAVEFORM_SINE);
      wait(1250);
      waveform1.amplitude(0);
      // Start the recording function
      startRecording();
      break;

    case Mode::Recording:
      // Handset is replaced
      if(buttonRecord.risingEdge()){
        // Debug log
        Serial.println("Stopping Recording");
        // Stop recording
        stopRecording();
        // Play audio tone to confirm recording has ended
        end_Beep();
      }
      else {
        continueRecording();
      }
      break;

    case Mode::Playing: // to make compiler happy
      break;  

    case Mode::Initialising: // to make compiler happy
      break;  
  }   
  
  MTP.loop();  // This is mandatory to be placed in the loop code.

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    
    updateVolume();
    
  }
}

void setMTPdeviceChecks(bool nable)
{
  if (nable)
  {
    MTP.storage()->set_DeltaDeviceCheckTimeMS(MTPcheckInterval);
    Serial.print("En");
  }
  else
  {
    MTP.storage()->set_DeltaDeviceCheckTimeMS((uint32_t) -1);
    Serial.print("Dis");
  }
  Serial.println("abled MTP storage device checks");
}
  

#if defined(INSTRUMENT_SD_WRITE)
static uint32_t worstSDwrite, printNext;
#endif // defined(INSTRUMENT_SD_WRITE)

void startRecording() {
  setMTPdeviceChecks(false); // disable MTP device checks while recording
#if defined(INSTRUMENT_SD_WRITE)
  worstSDwrite = 0;
  printNext = 0;
#endif // defined(INSTRUMENT_SD_WRITE)
  // Find the first available file number
//  for (uint8_t i=0; i<9999; i++) { // BUGFIX uint8_t overflows if it reaches 255  
  for (uint16_t i=0; i<9999; i++) {   
    // Format the counter as a five-digit number with leading zeroes, followed by file extension
    snprintf(filename, 11, " %05d.wav", i);
    // Create if does not exist, do not open existing, write, sync after write
    if (!SD.exists(filename)) {
      break;
    }
  }
  frec = SD.open(filename, FILE_WRITE);
  Serial.println("Opened file !");
  if(frec) {
    Serial.print("Recording to ");
    Serial.println(filename);
    queue1.begin();
    mode = Mode::Recording; 
    print_mode();
    recByteSaved = 0L;
  }
  else {
    Serial.println("Couldn't open file to record!");
  }
}

void continueRecording() {
#if defined(INSTRUMENT_SD_WRITE)
  uint32_t started = micros();
#endif // defined(INSTRUMENT_SD_WRITE)
#define NBLOX 16  
  // Check if there is data in the queue
  if (queue1.available() >= NBLOX) {
    byte buffer[NBLOX*AUDIO_BLOCK_SAMPLES*sizeof(int16_t)];
    // Fetch 2 blocks from the audio library and copy
    // into a 512 byte buffer.  The Arduino SD library
    // is most efficient when full 512 byte sector size
    // writes are used.
    for (int i=0;i<NBLOX;i++)
    {
      memcpy(buffer+i*AUDIO_BLOCK_SAMPLES*sizeof(int16_t), queue1.readBuffer(), AUDIO_BLOCK_SAMPLES*sizeof(int16_t));
      queue1.freeBuffer();
    }
    // Write all 512 bytes to the SD card
    frec.write(buffer, sizeof buffer);
    recByteSaved += sizeof buffer;
  }
  
#if defined(INSTRUMENT_SD_WRITE)
  started = micros() - started;
  if (started > worstSDwrite)
    worstSDwrite = started;

  if (millis() >= printNext)
  {
    Serial.printf("Worst write took %luus\n",worstSDwrite);
    worstSDwrite = 0;
    printNext = millis()+250;
  }
#endif // defined(INSTRUMENT_SD_WRITE)
}

void stopRecording() {
  // Stop adding any new data to the queue
  queue1.end();
  // Flush all existing remaining data from the queue
  while (queue1.available() > 0) {
    // Save to open file
    frec.write((byte*)queue1.readBuffer(), AUDIO_BLOCK_SAMPLES*sizeof(int16_t));
    queue1.freeBuffer();
    recByteSaved += AUDIO_BLOCK_SAMPLES*sizeof(int16_t);
  }
  writeOutHeader();
  // Close the file
  frec.close();
  Serial.println("Closed file");
  mode = Mode::Ready; print_mode();
  setMTPdeviceChecks(true); // enable MTP device checks, recording is finished
}


void playAllRecordings() {
  // Recording files are saved in the root directory
  File dir = SD.open("/");
  
  while (true) {
    File entry =  dir.openNextFile();
    if (strstr(entry.name(), "greeting"))
    {
       entry =  dir.openNextFile();
    }
    if (!entry) {
      // no more files
      entry.close();
      end_Beep();
      break;
    }
    //int8_t len = strlen(entry.name()) - 4;
//    if (strstr(strlwr(entry.name() + (len - 4)), ".raw")) {
//    if (strstr(strlwr(entry.name() + (len - 4)), ".wav")) {
    // the lines above throw a warning, so I replace them with this (which is also easier to read):
    if (strstr(entry.name(), ".wav") || strstr(entry.name(), ".WAV")) {
      Serial.print("Now playing ");
      Serial.println(entry.name());
      // Play a short beep before each message
      waveform1.amplitude(beep_volume);
      wait(750);
      waveform1.amplitude(0);
      // Play the file
      playWav1.play(entry.name());
      mode = Mode::Playing; print_mode();
    }
    entry.close();

//    while (playWav1.isPlaying()) { // strangely enough, this works for playRaw, but it does not work properly for playWav
    while (!playWav1.isStopped()) { // this works for playWav
      buttonPlay.update();
      buttonRecord.update();
      // Button is pressed again
//      if(buttonPlay.risingEdge() || buttonRecord.risingEdge()) { // FIX
      if(buttonPlay.fallingEdge() || buttonRecord.risingEdge()) { 
        playWav1.stop();
        mode = Mode::Ready; print_mode();
        return;
      }   
    }
  }
  // All files have been played
  mode = Mode::Ready; print_mode();
}

void playLastRecording() {
  // Find the first available file number
  uint16_t idx = 0; 
  for (uint16_t i=0; i<9999; i++) {
    // Format the counter as a five-digit number with leading zeroes, followed by file extension
    snprintf(filename, 11, " %05d.wav", i);
    // check, if file with index i exists
    if (!SD.exists(filename)) {
     idx = i - 1;
     break;
      }
  }
      // now play file with index idx == last recorded file
      snprintf(filename, 11, " %05d.wav", idx);
      Serial.println(filename);
      playWav1.play(filename);
      mode = Mode::Playing; print_mode();
      while (!playWav1.isStopped()) { // this works for playWav
      buttonPlay.update();
      buttonRecord.update();
      // Button is pressed again
//      if(buttonPlay.risingEdge() || buttonRecord.risingEdge()) { // FIX
      if(buttonPlay.fallingEdge() || buttonRecord.risingEdge()) {
        playWav1.stop();
        mode = Mode::Ready; print_mode();
        return;
      }   
    }
      // file has been played
  mode = Mode::Ready; print_mode();  
  end_Beep();
}


// Retrieve the current time from Teensy built-in RTC
time_t getTeensy3Time(){
  return Teensy3Clock.get();
}

// Callback to assign timestamps for file system operations
void dateTime(uint16_t* date, uint16_t* time, uint8_t* ms10) {

  // Return date using FS_DATE macro to format fields.
  *date = FS_DATE(year(), month(), day());

  // Return time using FS_TIME macro to format fields.
  *time = FS_TIME(hour(), minute(), second());

  // Return low time bits in units of 10 ms.
  *ms10 = second() & 1 ? 100 : 0;
}

// Non-blocking delay, which pauses execution of main program logic,
// but while still listening for input 
void wait(unsigned int milliseconds) {
  elapsedMillis msec=0;

  while (msec <= milliseconds) {
    buttonRecord.update();
    buttonPlay.update();
    if (buttonRecord.fallingEdge()) Serial.println("Button (pin 0) Press");
    if (buttonPlay.fallingEdge()) Serial.println("Button (pin 1) Press");
    if (buttonRecord.risingEdge()) Serial.println("Button (pin 0) Release");
    if (buttonPlay.risingEdge()) Serial.println("Button (pin 1) Release");
  }
}


void writeOutHeader() { // update WAV header with final filesize/datasize

//  NumSamples = (recByteSaved*8)/bitsPerSample/numChannels;
//  Subchunk2Size = NumSamples*numChannels*bitsPerSample/8; // number of samples x number of channels x number of bytes per sample
  Subchunk2Size = recByteSaved - 42; // because we didn't make space for the header to start with! Lose 21 samples...
  ChunkSize = Subchunk2Size + 34; // was 36;
  frec.seek(0);
  frec.write("RIFF");
  byte1 = ChunkSize & 0xff;
  byte2 = (ChunkSize >> 8) & 0xff;
  byte3 = (ChunkSize >> 16) & 0xff;
  byte4 = (ChunkSize >> 24) & 0xff;  
  frec.write(byte1);  frec.write(byte2);  frec.write(byte3);  frec.write(byte4);
  frec.write("WAVE");
  frec.write("fmt ");
  byte1 = Subchunk1Size & 0xff;
  byte2 = (Subchunk1Size >> 8) & 0xff;
  byte3 = (Subchunk1Size >> 16) & 0xff;
  byte4 = (Subchunk1Size >> 24) & 0xff;  
  frec.write(byte1);  frec.write(byte2);  frec.write(byte3);  frec.write(byte4);
  byte1 = AudioFormat & 0xff;
  byte2 = (AudioFormat >> 8) & 0xff;
  frec.write(byte1);  frec.write(byte2); 
  byte1 = numChannels & 0xff;
  byte2 = (numChannels >> 8) & 0xff;
  frec.write(byte1);  frec.write(byte2); 
  byte1 = sampleRate & 0xff;
  byte2 = (sampleRate >> 8) & 0xff;
  byte3 = (sampleRate >> 16) & 0xff;
  byte4 = (sampleRate >> 24) & 0xff;  
  frec.write(byte1);  frec.write(byte2);  frec.write(byte3);  frec.write(byte4);
  byte1 = byteRate & 0xff;
  byte2 = (byteRate >> 8) & 0xff;
  byte3 = (byteRate >> 16) & 0xff;
  byte4 = (byteRate >> 24) & 0xff;  
  frec.write(byte1);  frec.write(byte2);  frec.write(byte3);  frec.write(byte4);
  byte1 = blockAlign & 0xff;
  byte2 = (blockAlign >> 8) & 0xff;
  frec.write(byte1);  frec.write(byte2); 
  byte1 = bitsPerSample & 0xff;
  byte2 = (bitsPerSample >> 8) & 0xff;
  frec.write(byte1);  frec.write(byte2); 
  frec.write("data");
  byte1 = Subchunk2Size & 0xff;
  byte2 = (Subchunk2Size >> 8) & 0xff;
  byte3 = (Subchunk2Size >> 16) & 0xff;
  byte4 = (Subchunk2Size >> 24) & 0xff;  
  frec.write(byte1);  frec.write(byte2);  frec.write(byte3);  frec.write(byte4);
  frec.close();
  Serial.println("header written"); 
  Serial.print("Subchunk2: "); 
  Serial.println(Subchunk2Size); 
}

void end_Beep(void) {
          waveform1.frequency(523.25);
        waveform1.amplitude(beep_volume);
        wait(250);
        waveform1.amplitude(0);
        wait(250);
        waveform1.amplitude(beep_volume);
        wait(250);
        waveform1.amplitude(0);
        wait(250);
        waveform1.amplitude(beep_volume);
        wait(250);
        waveform1.amplitude(0);
        wait(250);
        waveform1.amplitude(beep_volume);
        wait(250);
        waveform1.amplitude(0);
}

void print_mode(void) { // only for debugging
  Serial.print("Mode switched to: ");
  // Initialising, Ready, Prompting, Recording, Playing
  if(mode == Mode::Ready)           Serial.println(" Ready");
  else if(mode == Mode::Prompting)  Serial.println(" Prompting");
  else if(mode == Mode::Recording)  Serial.println(" Recording");
  else if(mode == Mode::Playing)    Serial.println(" Playing");
  else if(mode == Mode::Initialising)  Serial.println(" Initialising");
  else Serial.println(" Undefined");
}
