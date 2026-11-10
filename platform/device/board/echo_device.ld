ENTRY(device_reset)

MEMORY
{
    DATA_RAM (rw) : ORIGIN = 0x03004000, LENGTH = 0x48000
    CODE_RAM (rx) : ORIGIN = 0x03050000, LENGTH = 0x2666C
    HIGH_RAM (rwx) : ORIGIN = 0x0307A000, LENGTH = 0x15000
    LANGUAGE_RAM (rw) : ORIGIN = 0x0308F000, LENGTH = 0x2200
    GLYPH_RAM (rw) : ORIGIN = 0x03091200, LENGTH = 0x8000
    RESIDENT_RAM (rwx) : ORIGIN = 0x03099200, LENGTH = 0x5E00
}

SECTIONS
{
    .code :
    {
        KEEP(*(.code_vectors))
        EXCLUDE_FILE(*libecho_playback.a:embedded_lyrics.c.obj *libecho_playback.a:lyrics.c.obj *libecho_app.a:scrobbler_log.c.obj *libecho_app.a:settings_screens.c.obj *libecho_app.a:cold_screens.c.obj *libecho_app.a:diagnostics_rows.c.obj *libecho_app.a:diagnostics_values.c.obj *libecho_app.a:diagnostics_menu.c.obj *libecho_app.a:diagnostics_menu_release.c.obj *libecho_app.a:equalizer_files.c.obj *libecho_app.a:equalizer_screen.c.obj *libecho_app.a:bluetooth_screen.c.obj *libecho_app.a:date_time_screen.c.obj *libecho_app.a:folders_screen.c.obj *libecho_app.a:test_pattern_screen.c.obj *libecho_app.a:stopwatch_screen.c.obj *libecho_app.a:font_sample_screen.c.obj *libecho_playback.a:replay_gain.c.obj *libecho_app.a:play_lists.c.obj *libecho_library.a:play_log.c.obj) *(.text .text.*)
        EXCLUDE_FILE(*libecho_playback.a:embedded_lyrics.c.obj *libecho_playback.a:lyrics.c.obj *libecho_app.a:scrobbler_log.c.obj *libecho_app.a:settings_screens.c.obj *libecho_app.a:cold_screens.c.obj *libecho_app.a:diagnostics_rows.c.obj *libecho_app.a:diagnostics_values.c.obj *libecho_app.a:diagnostics_menu.c.obj *libecho_app.a:diagnostics_menu_release.c.obj *libecho_app.a:equalizer_files.c.obj *libecho_app.a:equalizer_screen.c.obj *libecho_app.a:bluetooth_screen.c.obj *libecho_app.a:date_time_screen.c.obj *libecho_app.a:folders_screen.c.obj *libecho_app.a:test_pattern_screen.c.obj *libecho_app.a:stopwatch_screen.c.obj *libecho_app.a:font_sample_screen.c.obj *libecho_playback.a:replay_gain.c.obj *libecho_app.a:play_lists.c.obj *libecho_library.a:play_log.c.obj) *(.rodata .rodata.*)
        . = ALIGN(4);
        device_code_end = .;
    } > CODE_RAM

    .data :
    {
        KEEP(*(.entry_vector))
        *(.data .data.*)
        . = ALIGN(4);
    } > DATA_RAM AT > CODE_RAM

    .bss (NOLOAD) :
    {
        device_bss_start = .;
        *(.bss .bss.*)
        . = ALIGN(4);
        device_bss_end = .;
    } > DATA_RAM

    .noinit (NOLOAD) :
    {
        *(.noinit .noinit.*)
        . = ALIGN(4);
        device_noinit_end = .;
    } > DATA_RAM

    .high_ram (NOLOAD) :
    {
        *(.high_ram .high_ram.*)
        . = ALIGN(4);
    } > HIGH_RAM

    .cold :
    {
        device_cold_start = .;
        LONG(0) LONG(0) LONG(0) LONG(0)
        *libecho_app.a:settings_screens.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        *libecho_app.a:cold_screens.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        *libecho_app.a:diagnostics_rows.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        *libecho_app.a:diagnostics_values.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        *libecho_app.a:diagnostics_menu.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        *libecho_app.a:diagnostics_menu_release.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        *libecho_app.a:equalizer_files.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        *libecho_app.a:equalizer_screen.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        *libecho_app.a:bluetooth_screen.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        *libecho_app.a:date_time_screen.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        *libecho_app.a:folders_screen.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        *libecho_app.a:test_pattern_screen.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        *libecho_app.a:stopwatch_screen.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        *libecho_app.a:font_sample_screen.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        *libecho_playback.a:replay_gain.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        *libecho_app.a:play_lists.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        *libecho_library.a:play_log.c.obj(.text .text.* .rodata .rodata.* .resident .resident.*)
        . = ALIGN(4);
        device_cold_end = .;
    } > HIGH_RAM

    .language_ram (NOLOAD) :
    {
        *(.language_ram .language_ram.*)
        . = ALIGN(4);
    } > LANGUAGE_RAM

    .glyph_ram (NOLOAD) :
    {
        *(.glyph_ram .glyph_ram.*)
        . = ALIGN(4);
    } > GLYPH_RAM

    .resident :
    {
        device_resident_start = .;
        LONG(0) LONG(0) LONG(0) LONG(0)
        *(.resident .resident.*)
        *libecho_playback.a:embedded_lyrics.c.obj(.text .text.* .rodata .rodata.*)
        *libecho_playback.a:lyrics.c.obj(.text .text.* .rodata .rodata.*)
        *libecho_app.a:scrobbler_log.c.obj(.text .text.* .rodata .rodata.*)
        . = ALIGN(4);
        device_resident_end = .;
    } > RESIDENT_RAM

    .comment 0 : { *(.comment) }
    .ARM.attributes 0 : { *(.ARM.attributes) }

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }
}

ASSERT(ADDR(.code) == 0x03050000, "the code vector table must be at 0x03050000")
ASSERT(ADDR(.data) == 0x03004000, "the entry vector must be at 0x03004000")
ASSERT(SIZEOF(.data) == 8, "only the entry vector may be in .data: make initialised globals const")
ASSERT(device_noinit_end <= 0x0304C000, ".data, .bss and .noinit must end below 0x0304C000")
ASSERT(ADDR(.resident) == 0x03099200, "the resident tables must start at 0x03099200")
ASSERT(device_cold_end <= 0x0308F000, "the cold code must end below the language pack at 0x0308F000")
