#ifndef DMX_CLIENTAPI_H
#define DMX_CLIENTAPI_H

#ifdef __cplusplus
extern "C" {
#endif

/**\brief Init Secure Demux client
 * \param[in] use_ts_clone The ts clone enable statue
 * \retval SECDMX_SUCCESS On Success
 * \return Error code.
 */
int SECDMX_Init(int use_ts_clone);

/**\brief Deinit Secure Demux client
 * \retval SECDMX_SUCCESS On Success
 * \return Error code.
 */
int SECDMX_Deinit(void);

/**\brief Allocate DVR Buffer for specific frontend device
 * \param[in] sid The frontend device id
 * \param[in,out] size The allocate and actually allocated size
 * \param[out] addr The DVR buffer address
 * \retval SECDMX_SUCCESS On Success
 * \return Error code.
 */
int SECDMX_AllocateDVRBuffer(int sid, size_t *size, size_t *addr);

/**\brief Free specific DVR buffer
 * \param[in] sid The front device id
 * \retval SECDMX_SUCCESS On Success
 * \return Error code.
 */
int SECDMX_FreeDVRBuffer(int sid);

/**\brief Add a output buffer for specific frontend device
 * \param[in] sid The frontend device id
 * \param[in] addr The output buffer address
 * \param[in] size The output buffer size
 * \param[out] handle The returned output buffer handle
 * \retval SECDMX_SUCCESS On Success
 * \return Error code.
 */
int SECDMX_AddOutputBuffer(int sid, size_t addr, size_t size, size_t *handle);

/**\brief Add DVR pids for specific output buffer
 * \param[in] handle The output buffer handle
 * \param[in] pids The recording pid array
 * \param[in] pid_num The pid number
 * \retval SECDMX_SUCCESS On Success
 * \return Error code.
 */
int SECDMX_AddDVRPids(size_t handle, uint16_t *pids, int pid_num);

/**\brief Remove specific output buffer
 * \param[in] sid The frontend device id
 * \param[in] buf_id The output buffer id
 * \retval SECDMX_SUCCESS On Success
 * \return Error code.
 */
int SECDMX_RemoveOutputBuffer(size_t handle);

/**\brief Get current data start address and data len of specific output buffer
 * \param[in] handle The output buffer handle
 * \param[out] start_addr The data start address
 * \param[out] len The data len
 * \retval SECDMX_SUCCESS On Success
 * \return Error code.
 */
int SECDMX_GetOutputBufferStatus(size_t handle, size_t *start_addr, size_t *len);

/**\brief Divide data of DVR buffer to multiple output buffers
 * \param[in] sid The frontend device id
 * \param[in] wp The writer pointer of hw demux
 * \retval SECDMX_SUCCESS On Success
 * \return Error code.
 */
int SECDMX_ProcessData(int sid, size_t wp);

//----------------------------------------------------------------------------
// Below is a separate functional module
//----------------------------------------------------------------------------

/**SECTS Indexer's flag.*/
enum {
  SECTS_INDEX_PUSI      = 0x01,     /**< PUSI type.*/
  SECTS_INDEX_IFRAME    = 0x02,     /**< IFrame type.*/
  SECTS_INDEX_PTS       = 0x04,     /**< PTS type.*/
};

/**SECTS Video stream format.*/
typedef enum {
  SECTS_VIDEO_FORMAT_MPEG2,         /**< MPEG2.*/
  SECTS_VIDEO_FORMAT_H264,          /**< H264.*/
  SECTS_VIDEO_FORMAT_HEVC           /**< HEVC.*/
} SECTS_VideoStreamFormat_t;

/**SECTS Indexer's PUSI state.*/
typedef enum {
  SECTS_INDEXER_PUSI_NONE,          /**< PUSI init state.*/
  SECTS_INDEXER_PUSI_PARSING,       /**< PUSI parsing state.*/
  SECTS_INDEXER_PUSI_DONE,          /**< PUSI parse done state.*/
  SECTS_INDEXER_PUSI_INVALID        /**< PUSI invalid state.*/
} SECTS_IndexerPusiState_t;

/**SECTS Indexer's Ring buffer.*/
typedef struct {
  size_t buffer;                    /**< Ring bufffer's address.*/
  size_t len;                       /**< Ring buffer's length.*/
  size_t w_offset;                  /**< Ring buffer's write offset.*/
  size_t r_offset;                  /**< Ring buffer's read offset.*/
  size_t size;                      /**< Ring buffer's data length.*/
} SECTS_IndexerRingBuffer_t;

/**SECTS Indexer's PUSI.*/
typedef struct {
  size_t start;                     /**< Start position of PUSI.*/
  size_t end;                       /**< End position of PUSI.*/
  int flags;                        /**< SECTS_INDEX_PUSI etc.*/
  uint64_t pts;                     /**< Pts of the PUSI.*/
  SECTS_IndexerPusiState_t state;   /**< State of the PUSI.*/
} SECTS_IndexerPusi_t;

/**\brief Init Secure TS client
 * \retval 0 On Success
 * \return Error code.
 */
int SECTS_Init(void);

/**\brief Open a secure TS indexer session
 * \param[out] session The created session
 * \retval 0 On Success
 * \return Error code.
 */
int SECTS_OpenSession(size_t *session);

/**\brief Close a secure TS indexer session
 * \param[in] session The created session
 * \retval 0 On Success
 * \return Error code.
 */
int SECTS_CloseSession(size_t session);

/**\brief Allocate a secure buffer for secure dvr
 * \param[in] session The session which the secure buffer belongs to
 * \param[in] size The secure buffer size
 * \retval a pointer On Success
 * \return buffer address.
 */
void *SECTS_AllocSecureBuffer(size_t session, size_t size);

/**\brief Free a secure buffer
 * \param[in] session The session which the secure buffer belongs to
 * \retval 0 On Success
 * \return Error code.
 */
int SECTS_FreeSecureBuffer(size_t session, void *p);

/**\brief Set video parameters for ts indexer
 * \param[in] session The session which the video belongs to
 * \param[in] pid The video pid
 * \param[in] pid The video format
 * \retval 0 On Success
 * \return Error code.
 */
int SECTS_SetVideoParams(size_t session, int pid, int format);

/**\brief Set audio parameters for ts indexer
 * \param[in] session The session which the audio belongs to
 * \param[in] pid The audio pid
 * \retval 0 On Success
 * \return Error code.
 */
int SECTS_SetAudioParams(size_t session, int pid);

/**\brief Parse the indexs from the ring buffer
 * \param[in] session The session which the ring buffer belongs to
 * \param[in,out] ringbuf The ringbu buffer struct
 * \param[in,out] pusi The pusi parsed from the ring buffer
 * \param[in] max_pusi_cnt The max pusi number that can stored
 * \retval 0 On Success
 * \return Error code.
 */
int SECTS_IndexerParse(
        size_t session,
        SECTS_IndexerRingBuffer_t *ringbuf,
        SECTS_IndexerPusi_t *pusi,
        size_t max_pusi_cnt);

/**\brief Deinit Secure TS client
 * \retval 0 On Success
 * \return Error code.
 */
int SECTS_Deinit(void);

#ifdef __cplusplus
}
#endif
#endif /* DMX_CLIENTAPI_H */
