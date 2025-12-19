/*
 * Cantata
 *
 * Copyright (c) 2011-2022 Craig Drummond <craig.p.drummond@gmail.com>
 *
 * ----
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "httpstream.h"
#include "mpdconnection.h"
#include "mpdstatus.h"
#include "gui/settings.h"
#include "support/globalstatic.h"
#include "support/configuration.h"
#ifndef LIBVLC_FOUND
#include <QtMultimedia/QMediaPlayer>
#endif
#include <QTimer>

static const int constPlayerCheckPeriod = 250;
static const int constMaxPlayStateChecks= 2000 / constPlayerCheckPeriod;

#include <QDebug>
static bool debugEnabled = false;
#define DBUG if (debugEnabled) qWarning() << metaObject()->className() << __FUNCTION__
void HttpStream::enableDebug()
{
    debugEnabled = true;
}

GLOBAL_STATIC(HttpStream, instance)

HttpStream::HttpStream(QObject *p)
    : QObject(p)
    , enabled(false)
    , muted(false)
    , state(MPDState_Inactive)
    , playStateChecks(0)
    , currentVolume(50)
    , unmuteVol(50)
    , playStateCheckTimer(nullptr)
    , player(nullptr)
{
}

void HttpStream::save() const
{
    Configuration config(metaObject()->className());
    config.set("volume", currentVolume);
}

void HttpStream::setEnabled(bool e)
{
    if (e == enabled) {
        return;
    }

    enabled = e;
    if (enabled) {
        connect(MPDConnection::self(), SIGNAL(streamUrl(QString)), this, SLOT(streamUrl(QString)));
        connect(MPDStatus::self(), SIGNAL(updated()), this, SLOT(updateStatus()));
        streamUrl(MPDConnection::self()->getDetails().streamUrl);
    } else {
        disconnect(MPDConnection::self(), SIGNAL(streamUrl(QString)), this, SLOT(streamUrl(QString)));
        disconnect(MPDStatus::self(), SIGNAL(updated()), this, SLOT(updateStatus()));
        if (player) {
            save();
            #ifdef LIBVLC_FOUND
            libvlc_media_player_stop(player);
            #else
            player->stop();
            #endif
        }
    }
    emit isEnabled(enabled);
}

void HttpStream::setVolume(int vol)
{
    DBUG << vol;
    if (player) {
        currentVolume = vol;
        #ifdef LIBVLC_FOUND
        libvlc_audio_set_volume(player, vol);
        #else
// FIXED ‘class QMediaPlayer’ has no member named ‘setVolume’
        player->audioOutput()->setVolume(vol);
        #endif
        emit update();
    }
}

int HttpStream::volume()
{
    if (!enabled) {
        return -1;
    }

    int vol = currentVolume;
    if (player && !isMuted()) {
        #ifdef LIBVLC_FOUND
        vol = libvlc_audio_get_volume(player);
        #else
// FIXED ‘class QMediaPlayer’ has no member named ‘volume’
        vol = player->audioOutput()->volume();
        #endif
        if (vol < 0) {
            vol = currentVolume;
        } else {
            currentVolume = vol;
        }
    }
    DBUG << vol;
    return vol;
}

void HttpStream::toggleMute()
{
    DBUG << isMuted();
    if (player) {
        muted =! muted;
        #ifdef LIBVLC_FOUND
        libvlc_audio_set_mute(player, muted);
        #else
// FIXED ‘class QMediaPlayer’ has no member named ‘setMuted’
// NOTE This method exists in QAudioInput (or output? which is this?) now, which should be connected somehow...
        player->audioOutput()->setMuted(!muted);
        #endif
        emit update();
    }
}

// We get a string representing a URL. And...
void HttpStream::streamUrl(const QString &url)
{
    DBUG << url;
    // NOTE Is this just to clear an existing stream? Initialise the new one?
    // If the existing player is different from the passed URL, clear the player.
    #ifdef LIBVLC_FOUND
    if (player) {
        libvlc_media_player_stop(player);
        libvlc_media_player_release(player);
        libvlc_release(instance);
        player = 0;
    }
    #else
    if (player) {
        // FIXED? QMediaContent has been removed, replace with QUrl
        // ... but what is the purpose here? QUrl methods dont relate much
        // QMediaContent media = player->media();
        QUrl media = player->source();
        //if (media != nullptr && media.request().url() != url) {
        if ((!media.isEmpty()) && media.toString() != url) {
            player->stop();
            player->deleteLater();
            player = nullptr;
        }
    }
    #endif
    // we make a QUrl from the given string.
    QUrl qUrl(url);
    // if the QUrl is good and we don't have an existing player (i.e. we just scrubbed it)
    if (!url.isEmpty() && qUrl.isValid() && qUrl.scheme().startsWith("http") && !player) {
        #ifdef LIBVLC_FOUND
        instance = libvlc_new(0, NULL);
        QByteArray byteArrayUrl = url.toUtf8();
        media = libvlc_media_new_location(instance, byteArrayUrl.constData());
        player = libvlc_media_player_new_from_media(media);
        libvlc_media_release(media);
        #else
        player = new QMediaPlayer(this);
        player->setSource(qUrl);
// FIXED  ‘bufferStatusChanged’ is not a member of ‘QMediaPlayer’
// this is a signals / slots thing: sender, signal, receiver, method, [implicit auto connection type]
// ...player, buffering changed, tell this httpstream,
// ...we need an equivalent signal to bufferStatusChanged, perhaps MediaStatus:
// https://doc.qt.io/qt-6/qmediaplayer.html#MediaStatus-enum
// ..but in fact bufferProgressChanged seems to fit better.
// FIXED slot requires more arguments than the signal provides
//        connect(player, &QMediaPlayer::bufferStatusChanged, this,
        connect(player, &QMediaPlayer::bufferProgressChanged, this, &HttpStream::bufferingProgress);
        #endif
        muted = false;
        setVolume(Configuration(metaObject()->className()).get("volume", currentVolume));
    }
    if (player) {
        state = 0xFFFF; // Force an update...
        updateStatus();
    } else {
        state = MPDState_Inactive;
    }
    emit update();
}

#ifndef LIBVLC_FOUND
// NOTE this is important for an issue relating to bufferstatuschanged signals
// this is a SLOT so is triggered by some signal somewhere
// I think this says, "if the buffer is 100% full then play the stream, else haud on a bit"
void HttpStream::bufferingProgress(int progress)
{
    MPDStatus * const status = MPDStatus::self();
    if (status->state() == MPDState_Playing) {
        if (progress == 100) {
            player->play();
        } else {
            player->pause();
        }
    }
}
#endif

void HttpStream::updateStatus()
{
    if (!player) {
        return;
    }

    MPDStatus * const status = MPDStatus::self();
    DBUG << status->state() << state;

    // evaluates to true when it is needed to start or restart media player
    bool playerNeedsToStart = status->state() == MPDState_Playing;
    #ifdef LIBVLC_FOUND
    playerNeedsToStart = playerNeedsToStart && libvlc_media_player_get_state(player) != libvlc_Playing;
    #else
    // FIXED ‘class QMediaPlayer’ has no member named ‘state’
    // also, this is a very unreadable line construction with assignment and comparison together
    playerNeedsToStart = playerNeedsToStart &&
        (player->playbackState() == QMediaPlayer::StoppedState);
    #endif

    if (status->state() == state && !playerNeedsToStart) {
        return;
    }

    state = status->state();
    switch (status->state()) {
    case MPDState_Playing:
        // Only start playback if not aready playing
        if (playerNeedsToStart) {
            #ifdef LIBVLC_FOUND
            libvlc_media_player_play(player);
            startTimer();
            #else
// FIXED ‘class QMediaPlayer’ has no member named ‘media’; did you mean ‘NoMedia’?
// FIXME but ..what are we doing here, this goes in a circle. get url to set  url?!
            QUrl url = player->source();
            player->setSource(url);
            #endif
        }
        break;
    case MPDState_Paused:
    case MPDState_Inactive:
    case MPDState_Stopped:
        #ifdef LIBVLC_FOUND
        libvlc_media_player_stop(player);
        stopTimer();
        #else
        player->stop();
        #endif
        break;
    default:
        #ifdef LIBVLC_FOUND
        stopTimer();
        #endif
        break;
    }
}

void HttpStream::checkPlayer()
{
    #ifdef LIBVLC_FOUND
    if (0 == --playStateChecks) {
        stopTimer();
        DBUG << "Max checks reached";
    }
    if (libvlc_media_player_get_state(player) == libvlc_Playing) {
        DBUG << "Playing";
        stopTimer();
    } else {
        DBUG << "Try again";
        libvlc_media_player_play(player);
    }
    #endif
}

void HttpStream::startTimer()
{
    if (!playStateCheckTimer) {
        playStateCheckTimer = new QTimer(this);
        playStateCheckTimer->setSingleShot(false);
        playStateCheckTimer->setInterval(constPlayerCheckPeriod);
        connect(playStateCheckTimer, SIGNAL(timeout()), SLOT(checkPlayer()));
    }
    playStateChecks = constMaxPlayStateChecks;
    DBUG << playStateChecks;
    playStateCheckTimer->start();
}

void HttpStream::stopTimer()
{
    if (playStateCheckTimer) {
        DBUG;
        playStateCheckTimer->stop();
    }
    playStateChecks = 0;
}

#include "moc_httpstream.cpp"
