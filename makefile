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

# xenon.rel used to depend ONLY on xenon.c, so a changed map.h triggered no
# rebuild ('make' reported "up to date" and an outdated ROM got flashed).
# The headers are therefore listed explicitly as prerequisites; the build itself
# still goes through the .c.rel inference rule below.
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
