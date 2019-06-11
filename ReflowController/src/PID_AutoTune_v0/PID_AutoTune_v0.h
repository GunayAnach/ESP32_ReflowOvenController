#ifndef PID_AutoTune_v0
#define PID_AutoTune_v0
#define LIBRARY_VERSION	0.0.1

class PID_ATune
{


  public:
  //commonly used functions **************************************************************************
    PID_ATune(float*, float*);                       	// * Constructor.  links the Autotune to a given PID
    int Runtime();						   			   	// * Similar to the PID Compue function, returns non 0 when done
	void Cancel();									   	// * Stops the AutoTune	
	
	void SetOutputStep(float);						   	// * how far above and below the starting value will the output step?	
	float GetOutputStep();							   	// 
	
	void SetControlType(int); 						   	// * Determies if the tuning parameters returned will be PI (D=0)
	int GetControlType();							   	//   or PID.  (0=PI, 1=PID)			
	
	void SetLookbackSec(int);							// * how far back are we looking to identify peaks
	int GetLookbackSec();								//
	
	void SetNoiseBand(float);							// * the autotune will ignore signal chatter smaller than this value
	float GetNoiseBand();								//   this should be acurately set
	
	float GetKp();										// * once autotune is complete, these functions contain the
	float GetKi();										//   computed tuning parameters.  
	float GetKd();										//
	
  private:
    void FinishUp();
	bool isMax, isMin;
	float *input, *output;
	float setpoint;
	float noiseBand;
	int controlType;
	bool running;
	unsigned long peak1, peak2, lastTime;
	int sampleTime;
	int nLookBack;
	int peakType;
	float lastInputs[101];
    float peaks[10];
	int peakCount;
	bool justchanged;
	bool justevaled;
	float absMax, absMin;
	float oStep;
	float outputStart;
	float Ku, Pu;
	
};
#endif

