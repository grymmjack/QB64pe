$CONSOLE
$SCREENHIDE
_DEST _CONSOLE

s = _SNDOPEN("foobar.mp3", "VOL,PAUSE,SETPOS"): _SNDLOOP s
_SNDCLOSE s

print "Sound";

SYSTEM
