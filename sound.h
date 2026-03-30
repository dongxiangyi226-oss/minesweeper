#ifndef SOUND_H
#define SOUND_H

/* Initialize sound system */
void sound_init(void);

/* Play sound effects (non-blocking) */
void sound_click(void);
void sound_flag(void);
void sound_explode(void);
void sound_win(void);
void sound_hint(void);

#endif /* SOUND_H */
