#include "Arduino.h"

#include "PID_AutoTune_v0.h"


PID_ATune::PID_ATune(float* Input, float* Output)
{
	input = Input;
	output = Output;
	controlType =0 ; //default to PI
	noiseBand = 0.5;
	running = false;
	oStep = 30;
	SetLookbackSec(10);
	lastTime = millis();
	
}



void PID_ATune::Cancel()
{
	running = false;
} 
 
int PID_ATune::Runtime()
{
	justevaled=false;
	if(peakCount>9 && running)
	{
		running = false;
		FinishUp();
		return 1;
	}
	unsigned long now = millis();
	
	if((now-lastTime)<sampleTime) return false;
	lastTime = now;
	float refVal = *input;
	justevaled=true;
	if(!running)
	{ //initialize working variables the first time around
		peakType = 0;
		peakCount=0;
		justchanged=false;
		absMax=refVal;
		absMin=refVal;
		setpoint = refVal;
		running = true;
		outputStart = *output;
		*output = outputStart+oStep;
	}
	else
	{
		if(refVal>absMax)absMax=refVal;
		if(refVal<absMin)absMin=refVal;
	}
	
	//oscillate the output base on the input's relation to the setpoint
	
	if(refVal>setpoint+noiseBand) *output = outputStart-oStep;
	else if (refVal<setpoint-noiseBand) *output = outputStart+oStep;
	
	
  //bool isMax=true, isMin=true;
  isMax=true;isMin=true;
  //id peaks
  for(int i=nLookBack-1;i>=0;i--)
  {
    float val = lastInputs[i];
    if(isMax) isMax = refVal>val;
    if(isMin) isMin = refVal<val;
    lastInputs[i+1] = lastInputs[i];
  }
  lastInputs[0] = refVal;  
  if(nLookBack<9)
  {  //we don't want to trust the maxes or mins until the inputs array has been filled
	return 0;
	}
  
  if(isMax)
  {
    if(peakType==0)peakType=1;
    if(peakType==-1)
    {
      peakType = 1;
      justchanged=true;
      peak2 = peak1;
    }
    peak1 = now;
    peaks[peakCount] = refVal;
   
  }
  else if(isMin)
  {
    if(peakType==0)peakType=-1;
    if(peakType==1)
    {
      peakType=-1;
      peakCount++;
      justchanged=true;
    }
    
    if(peakCount<10)peaks[peakCount] = refVal;
  }
  
  if(justchanged && peakCount>2)
  { //we've transitioned.  check if we can autotune based on the last peaks
    float avgSeparation = (abs(peaks[peakCount-1]-peaks[peakCount-2])+abs(peaks[peakCount-2]-peaks[peakCount-3]))/2;
    if( avgSeparation < 0.05*(absMax-absMin))
    {
		FinishUp();
      running = false;
	  return 1;
	 
    }
  }
   justchanged=false;
	return 0;
}
void PID_ATune::FinishUp()
{
	  *output = outputStart;
      //we can generate tuning parameters!
      Ku = 4*(2*oStep)/((absMax-absMin)*3.14159);
      Pu = (float)(peak1-peak2) / 1000;
}

float PID_ATune::GetKp()
{
  switch(controlType){
    case CT_PI:
      return 0.45 * Ku;
    case CT_PID:
      return 0.60 * Ku;
    case CT_PID_NO_OVERSHOOT:
      return 0.20 * Ku;
  }
}

float PID_ATune::GetKi()
{
  switch(controlType){
    case CT_PI:
      return 0.54 * Ku / Pu;
    case CT_PID:
      return 1.20 * Ku / Pu;
    case CT_PID_NO_OVERSHOOT:
      return 0.40 * Ku / Pu; 
  }
}

float PID_ATune::GetKd()
{
  switch(controlType){
    case CT_PI:
      return 0;
    case CT_PID:
      return 0.075 * Ku * Pu;
    case CT_PID_NO_OVERSHOOT:
      return 0.066 * Ku * Pu;
  }
}

void PID_ATune::SetOutputStep(float Step)
{
	oStep = Step;
}

float PID_ATune::GetOutputStep()
{
	return oStep;
}

void PID_ATune::SetControlType(int Type) //0=PI, 1=PID
{
	controlType = Type;
}
int PID_ATune::GetControlType()
{
	return controlType;
}
	
void PID_ATune::SetNoiseBand(float Band)
{
	noiseBand = Band;
}

float PID_ATune::GetNoiseBand()
{
	return noiseBand;
}

void PID_ATune::SetLookbackSec(int value)
{
    if (value<1) value = 1;
	
	if(value<25)
	{
		nLookBack = value * 4;
		sampleTime = 250;
	}
	else
	{
		nLookBack = 100;
		sampleTime = value*10;
	}
}

int PID_ATune::GetLookbackSec()
{
	return nLookBack * sampleTime / 1000;
}
