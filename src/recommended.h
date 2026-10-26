#pragma once

// The Recommended tab: fun and useful firmware for the Cardputer ADV, picked so there is
// something for everyone, explained in plain words. Every pick's author says it supports
// the ADV (its keyboard chip differs from the original Cardputer's, so older firmware can't
// read the keys), checked against each project's own page, and each installs on an ADV.
// Ordered so the first screen shows different kinds of apps.
// IDs are LauncherHub/M5Burner firmware IDs; version, size and download come from LauncherHub
// when a details page opens. Kept in flash, not RAM.
struct RecInfo {
    const char* id;
    const char* name;
    const char* author;
    const char* tagline;  // one line under the name in the list
    const char* about;
    const char* good;     // "Good for" tags, separated by '|'
};

constexpr RecInfo RECOMMENDED[] = {
    {"cf30cb4c51c3b01de0eaef00ce0601a3", "Game Station", "geo_tp",
     "NES, Game Boy, Genesis and more",
     "Play classic console games: NES, Game Boy, SNES, Game Gear, Master System, Genesis, Atari, "
     "PC Engine and more. Put your own game files (ROMs) on the SD card.",
     "Gamers|Retro fans|Needs SD card"},
    {"783d79084a9a12f7b9205a3033aa8d3a", "MP3 Player", "sanchitminda",
     "Music from your SD card",
     "Copy MP3 files onto the SD card and listen, with folders, playlists and shuffle. It handles "
     "thousands of songs.",
     "Music lovers|Offline listening|Needs SD card"},
    {"95447ed62fe38eedd4df749c111b3177", "Ultimate Remote", "geo_tp",
     "A remote for almost any TV",
     "Turns the Cardputer's infrared light into a universal remote. It knows about 3,500 remotes "
     "from 636 brands and can scan to find the one that works with your TV.",
     "Everyone|TVs and projectors|No setup"},
    {"783b862c3d204807dd7510f820c057bc", "MicroPython Shell", "BeanpieChen",
     "Write and run Python code",
     "Type Python and see it run right away, with a built-in text editor to write and save "
     "programs. A great first step into coding.",
     "Learning to code|Students|Python"},
    {"a6518c427f3ee8fac780a6d5a80d34a1", "Piano Tiles", "Zeloksa",
     "Rhythm game with 54 songs",
     "Hit the keys in time as the tiles fall. 54 songs in five packs: classics, anime, games, "
     "movies and rock.",
     "Casual games|Music fans|Quick fun"},
    {"3bdb3c5e9f9ed09ef60ccb008d229fdb", "Cardputer ABC", "fancyoung",
     "Letters and sounds for toddlers",
     "Every key makes a sound. Letter keys sing a phonics song while the letter is drawn on "
     "screen; number keys play music from the SD card. Made for little hands that press everything.",
     "Toddlers|Learning letters|Parents"},
    {"db32af05402f82b01c8e08a4fc815220", "Web Radio", "halbeshuhn",
     "Thousands of radio stations",
     "Listen to internet radio. Search thousands of stations right on the Cardputer and keep "
     "the ones you like.",
     "Music lovers|News and talk|Needs Wi-Fi"},
    {"4753d7f172212dd33e9f451e20629bc4", "Chess", "beatsforthemind",
     "Play the computer or a friend",
     "Play chess against the computer, pass the Cardputer between two players, or play a friend "
     "on another Cardputer over the air.",
     "Thinkers|Two players|Works offline"},
    {"12fe96473eb3b299f79e1d17d178aa62", "MiniAcid", "urtubia",
     "A pocket techno groovebox",
     "A tiny music studio: two squelchy bass synths and punchy drums. Build patterns, let it "
     "make random ones, and jam along.",
     "Music makers|Electronic music|Creative"},
    {"818b54534b4ce93e1f389122e28ef1d9", "Keyboard & Mouse", "jayden_sudo",
     "Type on your PC, phone or tablet",
     "Use the Cardputer as a keyboard and mouse for your computer, phone or tablet, over "
     "Bluetooth or a USB cable. Tilt it in the air to move the pointer.",
     "Everyone|Phones and tablets|Presentations"},
    {"a2a1baeb43f390777b5bdffed5d0e248", "Doom", "szilamer",
     "The 1993 classic, made for ADV",
     "The classic shooter, rebuilt so the ADV keyboard works. Sound and music included, and two "
     "Cardputers can play together over a Grove cable. It is big, so it needs most of the space.",
     "Gamers|Nostalgia|Show it off"},
    {"8cdb180f18b1ad92ae5694fedafa270a", "Voice Recorder", "Moskic",
     "Record voice notes to SD",
     "Record voice notes and sounds to the SD card, then play, rename or delete them right on "
     "the Cardputer.",
     "Students|Notes on the go|Needs SD card"},
    {"d49a9b9d8050b9bfa65246b54fc87a18", "Bruce", "BruceDevices",
     "Radio and network toolkit",
     "A toolkit for exploring the wireless world: infrared, Wi-Fi, Bluetooth, RFID and more. "
     "Popular with tinkerers. Only use it on your own devices.",
     "Tinkerers|Wireless experiments|Advanced"},
    {"9ec7cfec63ca57dd65fd7ad412bcf7eb", "User Demo", "M5Stack",
     "Test keys, screen and sound",
     "M5Stack's own demo for the Cardputer ADV. Check that the keyboard, screen, speaker and "
     "microphone work.",
     "Testing hardware|Official M5Stack"},
};
