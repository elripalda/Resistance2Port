# What this port needs from the player's game copy. Read by tools\installer\gamefiles.ps1
# (used by setup.ps1 and the installer). Paths are relative to the folder that contains PS3_GAME.
@{
    GameName    = 'Resistance 2'
    TitleId     = 'BCES00226'
    RegionLabel = 'EU'
    AppVer      = '01.00'
    TitleMatch  = 'Resistance.?2'                 # other regions of the same game (for a clearer message)
    Required    = @('PS3_GAME/PARAM.SFO', 'PS3_GAME/USRDIR/EBOOT.BIN')
    Program     = @('PS3_GAME/USRDIR/EBOOT.BIN')   # the PS3 program file Setup decrypts
    Sha1        = @{
        'PS3_GAME/USRDIR/EBOOT.BIN' = 'cad595100af29baca3fe125642a5eaac647d51ec'
    }
    MinBytes    = 22500000000
    SizeLabel   = '23.3 GB'
    BuildBytes  = 12000000000                    # RPCS3 source, generated code, the build and the shader cache
    SaveFolder  = 'Resistance 2'                 # %USERPROFILE%\Saved Games\<this>
}