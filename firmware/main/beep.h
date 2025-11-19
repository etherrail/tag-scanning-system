#ifndef BEEP_HEADER
#define BEEP_HEADER

class Beep {
	public:
		Beep();

		void success();

	private:
		void play(int duration, int frequency);
};

#endif
