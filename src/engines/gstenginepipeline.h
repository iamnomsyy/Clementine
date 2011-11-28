/* This file is part of Clementine.
   Copyright 2010, David Sansome <me@davidsansome.com>

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef GSTENGINEPIPELINE_H
#define GSTENGINEPIPELINE_H

#include <QBasicTimer>
#include <QFuture>
#include <QMutex>
#include <QObject>
#include <QTimeLine>
#include <QUrl>

#include <gst/gst.h>
#include <boost/scoped_ptr.hpp>

#include "engine_fwd.h"

class GstElementDeleter;
class GstEngine;
class BufferConsumer;

struct GstURIDecodeBin;

class GstEnginePipeline : public QObject {
  Q_OBJECT

 public:
  GstEnginePipeline(GstEngine* engine);
  ~GstEnginePipeline();

  // Globally unique across all pipelines.
  int id() const { return id_; }

  // Call these setters before Init
  void set_output_device(const QString& sink, const QString& device);
  void set_replaygain(bool enabled, int mode, float preamp, bool compression);
  void set_buffer_duration_nanosec(qint64 duration_nanosec);

  // Creates the pipeline, returns false on error
  bool InitFromUrl(const QUrl& url, qint64 end_nanosec);
  bool InitFromString(const QString& pipeline);

  // BufferConsumers get fed audio data.  Thread-safe.
  void AddBufferConsumer(BufferConsumer* consumer);
  void RemoveBufferConsumer(BufferConsumer* consumer);
  void RemoveAllBufferConsumers();

  // Control the music playback
  QFuture<GstStateChangeReturn> SetState(GstState state);
  Q_INVOKABLE bool Seek(qint64 nanosec);
  void SetEqualizerEnabled(bool enabled);
  void SetEqualizerParams(int preamp, const QList<int>& band_gains);
  void SetVolume(int percent);
  void StartFader(qint64 duration_nanosec,
                  QTimeLine::Direction direction = QTimeLine::Forward,
                  QTimeLine::CurveShape shape = QTimeLine::LinearCurve);

  // If this is set then it will be loaded automatically when playback finishes
  // for gapless playback
  void SetNextUrl(const QUrl& url, qint64 beginning_nanosec, qint64 end_nanosec);
  bool has_next_valid_url() const { return next_url_.isValid(); }

  // Get information about the music playback
  QUrl url() const { return url_; }
  bool is_valid() const { return valid_; }
  // Please note that this method (unlike GstEngine's.position()) is
  // multiple-section media unaware.
  qint64 position() const;
  // Please note that this method (unlike GstEngine's.length()) is
  // multiple-section media unaware.
  qint64 length() const;
  // Returns this pipeline's state. May return GST_STATE_NULL if the state check
  // timed out. The timeout value is a reasonable default.
  GstState state() const;
  qint64 segment_start() const { return segment_start_; }

  QUrl redirect_url() const { return redirect_url_; }

  QString source_device() const { return source_device_; }

 public slots:
  void SetVolumeModifier(qreal mod);

 signals:
  void EndOfStreamReached(int pipeline_id, bool has_next_track);
  void MetadataFound(int pipeline_id, const Engine::SimpleMetaBundle& bundle);
  // This indicates an error, delegated from GStreamer, in the pipeline.
  // The message, domain and error_code are related to GStreamer's GError.
  void Error(int pipeline_id, const QString& message, int domain, int error_code);
  void FaderFinished();

 protected:
  void timerEvent(QTimerEvent *);

 private:
  // Static callbacks.  The GstEnginePipeline instance is passed in the last
  // argument.
  static GstBusSyncReply BusCallbackSync(GstBus*, GstMessage*, gpointer);
  static gboolean BusCallback(GstBus*, GstMessage*, gpointer);
  static void NewPadCallback(GstElement*, GstPad*, gpointer);
  static bool HandoffCallback(GstPad*, GstBuffer*, gpointer);
  static bool EventHandoffCallback(GstPad*, GstEvent*, gpointer);
  static void SourceDrainedCallback(GstURIDecodeBin*, gpointer);
  static void SourceSetupCallback(GstURIDecodeBin*, GParamSpec *pspec, gpointer);

  void TagMessageReceived(GstMessage*);
  void ErrorMessageReceived(GstMessage*);
  void ElementMessageReceived(GstMessage*);
  void StateChangedMessageReceived(GstMessage*);

  QString ParseTag(GstTagList* list, const char* tag) const;

  bool Init();
  GstElement* CreateDecodeBinFromString(const char* pipeline);

  void UpdateVolume();
  void UpdateEqualizer();
  bool ReplaceDecodeBin(GstElement* new_bin);
  bool ReplaceDecodeBin(const QUrl& url);

  void TransitionToNext();

  // If the decodebin is special (ie. not really a uridecodebin) then it'll have
  // a src pad immediately and we can link it after everything's created.
  void MaybeLinkDecodeToAudio();

 private slots:
  void FaderTimelineFinished();

 private:
  static const int kGstStateTimeoutNanosecs;
  static const int kFaderFudgeMsec;
  static const int kEqBandCount;
  static const int kEqBandFrequencies[];

  static GstElementDeleter* sElementDeleter;

  GstEngine* engine_;

  // Using == to compare two pipelines is a bad idea, because new ones often
  // get created in the same address as old ones.  This ID will be unique for
  // each pipeline.
  // Threading warning: access to the static ID field isn't protected by a
  // mutex because all pipeline creation is currently done in the main thread.
  static int sId;
  int id_;

  // General settings for the pipeline
  bool valid_;
  QString sink_;
  QString device_;

  // These get called when there is a new audio buffer available
  QList<BufferConsumer*> buffer_consumers_;
  QMutex buffer_consumers_mutex_;
  qint64 segment_start_;
  bool segment_start_received_;
  bool emit_track_ended_on_segment_start_;

  // Equalizer
  bool eq_enabled_;
  int eq_preamp_;
  QList<int> eq_band_gains_;

  // ReplayGain
  bool rg_enabled_;
  int rg_mode_;
  float rg_preamp_;
  bool rg_compression_;
  quint64 buffer_duration_nanosec_;

  // The URL that is currently playing, and the URL that is to be preloaded
  // when the current track is close to finishing.
  QUrl url_;
  QUrl next_url_;

  // If this is > 0 then the pipeline will be forced to stop when playback goes
  // past this position.
  qint64 end_offset_nanosec_;

  // We store the beginning and end for the preloading song too, so we can just
  // carry on without reloading the file if the sections carry on from each
  // other.
  qint64 next_beginning_offset_nanosec_;
  qint64 next_end_offset_nanosec_;

  // Set temporarily when moving to the next contiguous section in a multi-part
  // file.
  bool ignore_next_seek_;

  // Set temporarily when switching out the decode bin, so metadata doesn't
  // get sent while the Player still thinks it's playing the last song
  bool ignore_tags_;

  // When the gstreamer source requests a redirect we store the URL here and
  // callers can pick it up after the state change to PLAYING fails.
  QUrl redirect_url_;

  // When we need to specify the device to use as source (for CD device)
  QString source_device_;

  // Seeking while the pipeline is in the READY state doesn't work, so we have
  // to wait until it goes to PAUSED or PLAYING.
  // Also we have to wait for the decodebin to be connected.
  bool pipeline_is_initialised_;
  bool pipeline_is_connected_;
  qint64 pending_seek_nanosec_;

  int volume_percent_;
  qreal volume_modifier_;

  boost::scoped_ptr<QTimeLine> fader_;
  QBasicTimer fader_fudge_timer_;

  GstElement* pipeline_;

  // Bins
  // uridecodebin ! audiobin
  GstElement* uridecodebin_;
  GstElement* audiobin_;

  // Elements in the audiobin.  See comments in Init()'s definition.
  GstElement* queue_;
  GstElement* audioconvert_;
  GstElement* rgvolume_;
  GstElement* rglimiter_;
  GstElement* audioconvert2_;
  GstElement* equalizer_preamp_;
  GstElement* equalizer_;
  GstElement* volume_;
  GstElement* audioscale_;
  GstElement* audiosink_;

  uint bus_cb_id_;
};

#endif // GSTENGINEPIPELINE_H
