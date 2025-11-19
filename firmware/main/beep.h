#ifndef BEEP_HEADER
#define BEEP_HEADER

class Beep {
	public:
		void success();

	private:
		void play(int length, int frequency);
};

#endif
