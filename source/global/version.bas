DIM SHARED Version AS STRING
DIM SHARED IsCiVersion AS _BYTE

Version$ = "0.5.0"

' If ./internal/version.txt exist, then this is some kind of CI build with a label
If _FILEEXISTS("internal/version.txt") THEN
    versionfile = FREEFILE
    OPEN "internal/version.txt" FOR INPUT AS #versionfile

    LINE INPUT #versionfile, VersionLabel$
    Version$ = Version$ + VersionLabel$

    if VersionLabel$ <> "" then
        IsCiVersion = -1
    end if

    CLOSE #versionfile
END IF
