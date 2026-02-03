Cantata qt6 version
===================

**NOTE** **Development is taking place in the "qt6_port" branch.**

# What is Cantata?

Cantata is the program that I use on my laptop to play music through my Hi-Fi (via the raspberry Pi and hard drive connected to the amplifier).

In more technical language, Cantata is a program to control an MPD (Music Player Daemon) instance. It is the piece that you use to tell the music player *what* you want to listen to. That implies a bunch of features to make it friendly, attractive and useable - displaying cover art, searching (for songs, artists, albums, etc), and queuing tracks.

# What is this, specifically?

This is a friendly fork of a program that I have used for many years. The original author has archived the code and ceased development, and I am motivated to carry on being able to use the program for years to come. (My Hi-fi is [ahem] years old and I believe that the software part should keep running as long as the hardware does.)

The [original README document](./README_OLD.md) gives some of the history and features of the project.

Another part of the motivation for this project is that I have been learning C++ programming and since the toolkit for the program has a new version (this is Qt5 and Qt6 now exists), it needs someone to step up and do the work updating it if it is not to fall into disuse.

I don't know for sure I can do the work, but I have made decent progress so far. I am first to have a version that compiles against qt6 (and after that, one that runs ;) ). When that is achieved, I will look to simplify the codebase - some features are obsolete and some don't interest me enough to keep maintaining them. Then, I have some ideas of things that I'd like to improve or tweak, but that is for the future.
