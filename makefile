.SUFFIXES: .c .asm .rel .abs
NAME = xenon
OBJS = \
	xenon.rel \
	library.rel \
	sounds.rel \
	dmaprog.rel \


$(NAME).ngp: makefile ngpc.lcf $(OBJS)
	tulink -la -o $(NAME).abs ngpc.lcf system.lib $(OBJS)
	tuconv -Fs24 $(NAME).abs
	s242ngp $(NAME).s24

# 21.07.2026: xenon.rel hing NUR an xenon.c - eine geaenderte map.h loeste keinen
# Neubau aus ('make' meldete "up to date" und man flashte eine veraltete ROM).
# Header hier explizit als Voraussetzung eintragen; gebaut wird weiterhin ueber
# die .c.rel-Inferenzregel unten.
xenon.rel: xenon.c ngpc.h carthdr.h library.h worm_paths.h PNG/Map/map.h PNG/Logo/logo_tiles.h Musik/Titel.c sounds.h Musik/game_theme.c
sounds.rel: sounds.c sounds.h ngpc.h

.c.rel:
	cc900 -c -O3 $< -o $@

.asm.rel:
	asm900 -g $<

clean:
	del *.rel
	del *.abs
	del *.map 
	del *.s24
