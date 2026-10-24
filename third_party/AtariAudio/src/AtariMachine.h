//----------------------------------------------------------
//
//	AtariAudio 1.26
//	Small & accurate ATARI-ST audio emulation
//	by Arnaud Carré aka Leonard/Oxygene (@leonard_coder)
//
//----------------------------------------------------------
#pragma once
#include <stdint.h>
#include "ym2149c.h"
#include "Mk68901.h"
#include "SteDac.h"
#include "Blitter.h"
#include "external/Musashi/m68k.h"
#include "external/Musashi/m68kops.h"


class AtariMachine
{
public:
	AtariMachine();
	~AtariMachine();

	enum ExitCode
	{
		kCrash = (1 << 0),
		kReset = (1 << 1),
	};

	void		Startup(uint32_t hostReplayRate);
	bool		Upload(const void* src, uint32_t addr, uint32_t size);
	bool 		PlayerTick(uint32_t musicDriverCallAddr);
	bool		Jsr(uint32_t addr, uint32_t d0);
	int16_t		ComputeNextSample();
	uint32_t	ComputeCurrentVisualLevels() const;

	unsigned int	memRead8(unsigned int address);
	unsigned int	memRead16(unsigned int address);
	void			memWrite8(unsigned int address, unsigned int value);
	void			memWrite16(unsigned int address, unsigned int value);
	int				TrapInstructionCallback(int v);
	void			ResetCb(void);
	void 			IllegalCb(void);
	void 			MuteVoices(uint32_t muteMask);

	// STYM2149 (addition, see PROVENANCE.md): observer of the writes to the chip port (port 0: register select,
	// port 2: data), called just before each write, including those of Xbios(32) (DoSound). Also called with the
	// port YM_OBSERVER_TIMER_IRQ_END just after each exit from a timer interrupt
	// (Ym2149c::InsideTimerIrq(false), where the chip resets the "square-sync" edges).
	typedef void (*YmPortObserver)(void* user, uint8_t port, uint8_t value);
	static	const	uint8_t	YM_OBSERVER_TIMER_IRQ_END = 0xff;
	void		SetYmPortObserver(YmPortObserver observer, void* user) { m_ymObserver = observer; m_ymObserverUser = user; }

private:
	static	const	uint32_t	RAM_SIZE = 4*1024*1024;
	static	const	uint32_t	RTE_INSTRUCTION_ADDR = 0x500;
	static	const	uint32_t	RESET_INSTRUCTION_ADDR = 0x502;
	static	const	uint32_t	GEMDOS_MALLOC_EMUL_BUFFER = RAM_SIZE-0x100000;

	// Peter Johnson used Xbios(32) (DoSound) for Arkanoid and Wizball!!
	void 		DoSoundTick();
	void		ConfigureReturnByRts();
	void		ConfigureReturnByRte();
	bool		JmpBinary(uint32_t pc, int timeOut50Hz);
	void		Gemdos(int func, uint32_t a7);
	void		XBios(int func, uint32_t a7);
	void		XbiosTimerSet(int ctrlPort, int dataPort, int enablePort, int bit, int mask, int ctrlValue, int dataValue);

	uint32_t m_doSndPtr;
	uint8_t m_doSndVal;
	uint8_t m_doSndDelay;
	uint8_t*	m_RAM;
	int			m_exitCode;
	uint32_t	m_nextGemdosMallocAd;
	uint32_t 	m_muteMask;
	Ym2149c		m_ym2149;
	Mk68901		m_mfp;
	SteDac		m_steDac;
	Blitter 	m_blitter;
	M68k m_cpu;

	// STYM2149 (addition): write to the chip port, passed to the observer first.
	void		YmWritePort(uint8_t port, uint8_t value);
	YmPortObserver	m_ymObserver = nullptr;
	void*		m_ymObserverUser = nullptr;
};
