#ifndef SLIPPI_TIMER_HEADER
#define SLIPPI_TIMER_HEADER

#include <wx/timer.h>
#include <wx/stattext.h>
#include "DolphinWX/PlaybackSlider.h"

class CFrame;

class slippiTimer : public wxTimer
{
  public:
	slippiTimer(CFrame* mainFrame, PlaybackSlider *slider, wxStaticText *text) { 
		m_frame = mainFrame;
		m_slider = slider; 
		m_text = text;
	}

	// Called each time the timer's timeout expires
	void Notify() wxOVERRIDE;

	CFrame *m_frame;
	PlaybackSlider *m_slider;
	wxStaticText *m_text;

	bool hasSetRange = false;
};

#endif
