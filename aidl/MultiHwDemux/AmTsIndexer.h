#ifndef AMTSINDEXER_H
#include <utils/RefBase.h>

using namespace android;
/**Video format*/
typedef enum {
  TS_INDEXER_VIDEO_FORMAT_NONE,
  TS_INDEXER_VIDEO_FORMAT_MPEG2, /**< MPEG2*/
  TS_INDEXER_VIDEO_FORMAT_H264,  /**< H264*/
  TS_INDEXER_VIDEO_FORMAT_HEVC   /**< HEVC*/
} TS_Indexer_StreamFormat_t;

/**Event type.*/
typedef enum {
  TS_INDEXER_EVENT_TYPE_START_INDICATOR,            /**< TS start indicator.*/
  TS_INDEXER_EVENT_TYPE_DISCONTINUITY_INDICATOR,    /**< TS discontinuity indicator.*/
  TS_INDEXER_EVENT_TYPE_MPEG2_I_FRAME,              /**< MPEG2 I frame.*/
  TS_INDEXER_EVENT_TYPE_MPEG2_P_FRAME,              /**< MPEG2 P frame.*/
  TS_INDEXER_EVENT_TYPE_MPEG2_B_FRAME,              /**< MPEG2 B frame.*/
  TS_INDEXER_EVENT_TYPE_MPEG2_SEQUENCE,             /**< MPEG2 Video Sequence header.*/
  TS_INDEXER_EVENT_TYPE_AVC_I_SLICE,                /**< AVC I slice.*/
  TS_INDEXER_EVENT_TYPE_AVC_B_SLICE,                /**< AVC B slice.*/
  TS_INDEXER_EVENT_TYPE_AVC_P_SLICE,                /**< AVC P slice.*/
  TS_INDEXER_EVENT_TYPE_AVC_SI_SLICE,               /**< AVC SI slice.*/
  TS_INDEXER_EVENT_TYPE_AVC_SP_SLICE,               /**< AVC SP slice.*/
  TS_INDEXER_EVENT_TYPE_HEVC_SPS,                   /**< HEVC NAL unit type SPS_NUT.*/
  TS_INDEXER_EVENT_TYPE_HEVC_AUD,                   /**< HEVC NAL unit type AUD_NUT.*/
  TS_INDEXER_EVENT_TYPE_HEVC_BLA_W_LP,              /**< HEVC NAL unit type BLA_W_LP.*/
  TS_INDEXER_EVENT_TYPE_HEVC_BLA_W_RADL,            /**< HEVC NAL unit type BLA_W_RADL.*/
  TS_INDEXER_EVENT_TYPE_HEVC_BLA_N_LP,              /**< HEVC NAL unit type BLA_N_LP.*/
  TS_INDEXER_EVENT_TYPE_HEVC_IDR_W_RADL,            /**< HEVC NAL unit type IDR_W_RADL.*/
  TS_INDEXER_EVENT_TYPE_HEVC_IDR_N_LP,              /**< HEVC NAL unit type IDR_N_LP.*/
  TS_INDEXER_EVENT_TYPE_HEVC_TRAIL_CRA,             /**< HEVC NAL unit type TRAIL_CRA.*/
  TS_INDEXER_EVENT_TYPE_VIDEO_PTS,                  /**< MEPG2/AVC/HEVC PTS.*/
  TS_INDEXER_EVENT_TYPE_AUDIO_PTS                   /**< Audio PTS.*/
} TS_Indexer_EventType_t;

/**Stream Parser state.*/
typedef enum {
  TS_INDEXER_STATE_INIT,        /**< Init state.*/
  TS_INDEXER_STATE_TS_START,    /**< TS header state with start_indicator==1.*/
  TS_INDEXER_STATE_PES_HEADER,  /**< PES header state.*/
  TS_INDEXER_STATE_PES_PTS,     /**< PES pts state.*/
  TS_INDEXER_STATE_PES_I_FRAME  /**< PES I-frame state.*/
} TS_Indexer_State_t;

/**Event.*/
typedef struct {
  TS_Indexer_EventType_t type;   /**< Event type.*/
  int                    pid;    /**< The PID of the stream.*/
  uint64_t               offset; /**< The the TS packet offset of this event.*/
  uint64_t               pts;    /**< The PTS of this frame.*/
} TS_Indexer_Event_t;

/**TS indexer.*/
typedef struct TS_Indexer_s TS_Indexer_t;

/**Event callback function.*/
typedef void (*TS_Indexer_EventCallback_t) (TS_Indexer_t *ts_indexer, TS_Indexer_Event_t *event);

/**PES parser.*/
typedef struct {
  uint64_t                      pts;            /**< The a/v PTS.*/
  uint64_t                      offset;         /**< The current offset.*/
  uint8_t                       data[184+16];   /**< The PES data.*/
  int                           len;            /**< The length of PES data`.*/
  TS_Indexer_State_t            state;          /**< The stream state.*/
} PESParser;

/**TS parser.*/
typedef struct {
  int                           pid;    /**< The a/v PID.*/
  TS_Indexer_StreamFormat_t     format; /**< The a/v format.*/
  PESParser                     PES;    /**< The PES parser.*/
  uint64_t                      offset; /**< The offset of packet with start indicator.*/
} TSParser;

/**TS indexer.*/
struct TS_Indexer_s {
  TSParser               video_parser;  /**< The video parser.*/
  TSParser               audio_parser;  /**< The audio parser.*/
  uint64_t                      offset; /**< The current offset.*/
  TS_Indexer_EventCallback_t callback;  /**< The event callback function.*/
  void                   *user_data;    /**< The user data*/
};

class AmTsIndexer : public RefBase {
public:
    AmTsIndexer();
    ~AmTsIndexer();

    /**
     * Release the TS indexer.
     * \param ts_indexer The TS indexer to be released.
     */
    void ts_indexer_destroy();
    /**
     * Set the video format.
     * \param ts_indexer The TS indexer.
     * \param format The video format.
     * \retval 0 On success.
     * \retval -1 On error.
     */
    int ts_indexer_set_video_format(TS_Indexer_StreamFormat_t format);
    /**
     * Set the video PID.
     * \param ts_indexer The TS indexer.
     * \param pid The video PID.
     * \retval 0 On success.
     * \retval -1 On error.
     */
    int ts_indexer_set_video_pid(int pid);
    /**
     * Set the audio PID.
     * \param ts_indexer The TS indexer.
     * \param pid The audio PID.
     * \retval 0 On success.
     * \retval -1 On error.
     */
    int ts_indexer_set_audio_pid(int pid);
    /**
     * Set the event callback function.
     * \param ts_indexer The TS indexer.
     * \param callback The event callback function.
     * \retval 0 On success.
     * \retval -1 On error.
     */
    int ts_indexer_set_event_callback(TS_Indexer_EventCallback_t callback, void *user_data);
    /**
     * Parse the TS stream and generate the index data.
     * \param ts_indexer The TS indexer.
     * \param data The TS data.
     * \param len The length of the TS data in bytes.
     * \return The left TS data length of bytes.
     */
    int ts_indexer_parse (uint8_t *data, int len);

private:
    /**
     * Initialize the TS indexer.
     * \param ts_indexer The TS indexer to be initialized.
     * \retval 0 On success.
     * \retval -1 On error.
     */
    int ts_indexer_init(TS_Indexer_t *ts_indexer);
    TS_Indexer_t *mpts_indexer = NULL;
};
#endif

