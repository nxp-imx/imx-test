/*
 * Copyright 2023 NXP
 *
 */
/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include <imx-mm/parser/fsl_parser.h>
#include "pitcher/pitcher_def.h"
#include "pitcher/pitcher.h"
#include "pitcher/platform.h"
#include "mxc_v4l2_vpu_enc.h"

#ifndef FLAG_VIDEO_INSERT_HEADER
#define FLAG_VIDEO_INSERT_HEADER 0x100
#endif

#define call_void_op(parser, op, arg, ...) \
	do { \
		int ierr = (parser)->op ? (parser)->op(arg, ##__VA_ARGS__) : -101;\
		if (ierr < 0) {\
			PITCHER_ERR("call %s fail, ret = %d\n", #op, ierr);\
		} \
	} while (0)

#define call_vop(ret, exit_on_fail, parser, op, arg, ...) \
	do { \
		ret = (parser)->op ? (parser)->op(arg, ##__VA_ARGS__) : -101;\
		if (ret < 0) {\
			PITCHER_ERR("call %s fail, ret = %d\n", #op, ret);\
			ret = -RET_E_INVAL;\
			if (exit_on_fail) \
				goto exit; \
		} \
	} while (0)

#define LOAD_FUNC(parser, id, name)	\
	do {\
		parser->queryInterface(id, (void **)&parser->name); \
		if (!parser->name) \
			PITCHER_ERR("fail to load func %s : %s\n", #name, dlerror());\
	} while (0)

struct mm_parser_interface {
	void *library;
	tFslParserQueryInterface queryInterface;

	FslParserVersionInfo getVersionInfo;
	FslCreateParser createParser;
	FslDeleteParser deleteParser;
	FslCreateParser2 createParser2;

	/* index export/import */
	FslParserInitializeIndex initializeIndex;
	FslParserImportIndex importIndex;
	FslParserExportIndex exportIndex;

	/* movie properties */
	FslParserIsSeekable isSeekable;
	FslParserGetMovieDuration getMovieDuration;
	FslParserGetUserData getUserData;
	FslParserGetMetaData getMetaData;

	FslParserGetNumTracks getNumTracks;

	FslParserGetNumPrograms getNumPrograms;
	FslParserGetProgramTracks getProgramTracks;

	/* generic track properties */
	FslParserGetTrackType getTrackType;
	FslParserGetTrackDuration getTrackDuration;
	FslParserGetLanguage getLanguage;
	FslParserGetBitRate getBitRate;
	FslParserGetDecSpecificInfo getDecoderSpecificInfo;

	/* video properties */
	FslParserGetVideoFrameWidth getVideoFrameWidth;
	FslParserGetVideoFrameHeight getVideoFrameHeight;
	FslParserGetVideoFrameRate getVideoFrameRate;
	FslParserGetVideoFrameCount getVideoFrameCount;
	FslParserGetVideoFrameRotation getVideoFrameRotation;

	/* audio properties */
	FslParserGetAudioNumChannels getAudioNumChannels;
	FslParserGetAudioSampleRate getAudioSampleRate;
	FslParserGetAudioBitsPerSample getAudioBitsPerSample;
	FslParserGetAudioBlockAlign getAudioBlockAlign;
	FslParserGetAudioChannelMask getAudioChannelMask;
	FslParserGetAudioBitsPerFrame getAudioBitsPerFrame;

	/* text/subtitle properties */
	FslParserGetTextTrackWidth getTextTrackWidth;
	FslParserGetTextTrackHeight getTextTrackHeight;

	/* sample reading, seek & trick mode */
	FslParserGetReadMode getReadMode;
	FslParserSetReadMode setReadMode;

	FslParserEnableTrack enableTrack;

	FslParserGetNextSample getNextSample;
	FslParserGetNextSyncSample getNextSyncSample;

	FslParserGetFileNextSample getFileNextSample;
	FslParserGetFileNextSyncSample getFileNextSyncSample;
	FslParserSeek seek;
};

struct mm_extractor_test_t {
	struct test_node node;
	struct pitcher_unit_desc desc;
	int chnno;
	struct pix_fmt_info format;
	int end;

	struct mm_parser_interface parser;
	FslParserHandle handle;
	uint8_t *codec_data;
	uint32_t codec_data_size;
	bool codec_data_sent;

	const char *filename;
	uint32 read_mode;
	uint32 track_index;
	bool seekable;
	uint64 duration;
	uint32 decoder_type;
	uint32 sub_type;

	uint32_t sizeimage;
	uint32 width;
	uint32 height;
	uint32 bitrate;
	uint32 rate;
	uint32 scale;
	uint32 flag;
	uint32_t is_avcc;
	uint32_t is_hvcc;
	uint32_t length_size;

	unsigned long frame_count;

	struct pitcher_buffer *buffer;
};

struct mxc_vpu_test_option mm_extractor_options[] = {
	{"key",  1, "--key <key>\n\t\t\tassign key number"},
	{"name", 1, "--name <filename>\n\t\t\tassign input file name"},
	{"bs", 1, "--bs <bs count>\n\t\t\tSpecify the count of input buffer block size, the unit is Kb."},
	{NULL, 0, NULL},
};

int parse_mm_extractor_option(struct test_node *node,
				struct mxc_vpu_test_option *option,
				char *argv[])
{
	struct mm_extractor_test_t *mme;

	if (!node || !option || !option->name)
		return -RET_E_INVAL;
	if (option->arg_num && !argv)
		return -RET_E_INVAL;

	mme = container_of(node, struct mm_extractor_test_t, node);
	if (!strcasecmp(option->name, "key"))
		mme->node.key = strtol(argv[0], NULL, 0);
	else if (!strcasecmp(option->name, "name"))
		mme->filename = argv[0];
	else if (!strcasecmp(option->name, "bs"))
		mme->sizeimage = max(strtol(argv[0], NULL, 0), 256) * 1024;

	return RET_OK;
}


FslFileHandle mme_file_open(const uint8 *fileName, const uint8 *mode, void *context)
{
	struct mm_extractor_test_t *mme = context;

	if (fileName)
		PITCHER_LOG("parser open %s\n", fileName);

	if (!mme || !mme->filename)
		return NULL;

	return (FslFileHandle) fopen(mme->filename, (const char *)mode);
}

int32 mme_file_close(FslFileHandle handle, void *context)
{
	if (handle)
		fclose((FILE *) handle);

	return 0;
}

uint32 mme_file_read(FslFileHandle handle, void *buffer, uint32 size, void *context)
{
	int ret;

	if (!handle)
		return 0;
	ret = fread(buffer, 1, size, (FILE *) handle);
	if (ret > 0)
		return ret;
	return 0xffffffff;
}

int32 mme_file_seek(FslFileHandle handle, int64 offset, int32 whence, void *context)
{
	int ret;

	if (!handle)
		return -1;

	ret = fseek((FILE *) handle, offset, whence);
	if (ret < 0)
		return -1;
	return 0;
}

int64 mme_file_tell(FslFileHandle handle, void *context)
{
	if (!handle)
		return -1;
	return ftell((FILE *) handle);
}

int64 mme_file_size(FslFileHandle handle, void *context)
{
	FILE *fp = handle;
	int pos;
	int64 size;

	if (!fp)
		return -1;

	pos = ftell(fp);
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, pos, SEEK_SET);

	return size;
}

int64 mme_file_check_available_bytes(FslFileHandle handle, int64 bytesRequested, void *context)
{
	FILE *fp = handle;

	if (!fp)
		return 0;

	return bytesRequested;
}

uint32 mme_file_get_flag(FslFileHandle handle, void *context)
{
	if (!handle)
		return 0;

	return FILE_FLAG_READ_IN_SEQUENCE;
}

FslFileStream mme_file_ops = {
	.Open = mme_file_open,
	.Close = mme_file_close,
	.Read = mme_file_read,
	.Seek = mme_file_seek,
	.Tell = mme_file_tell,
	.Size = mme_file_size,
	.CheckAvailableBytes = mme_file_check_available_bytes,
	.GetFlag = mme_file_get_flag,
};

void *mme_calloc(uint32 nmemb, uint32 size)
{
	return pitcher_calloc(nmemb, size);
}

void *mme_malloc(uint32 size)
{
	return pitcher_malloc(size);
}

void mme_free(void *ptr)
{
	pitcher_free(ptr);
}

void *mme_realloc(void *ptr, uint32 size)
{
	return pitcher_realloc(ptr, size);
}

ParserMemoryOps mme_memory_ops = {
	.Calloc = mme_calloc,
	.Malloc = mme_malloc,
	.Free = mme_free,
	.ReAlloc = mme_realloc,
};

uint8 *mme_request_buffer(uint32 streamNum, uint32 *size, void **bufContext, void *parserContext)
{
	struct mm_extractor_test_t *mme = parserContext;

	if (!mme || !mme->buffer || !size || !(*size))
		return NULL;
	if (*size + mme->buffer->planes[0].bytesused > mme->buffer->planes[0].size)
		return NULL;

	return (uint8 *)(mme->buffer->planes[0].virt + mme->buffer->planes[0].bytesused);
}

void mme_release_buffer(uint32 streamNum, uint8 *pBuffer, void *bufContext, void *parserContext)
{
}

ParserOutputBufferOps mme_buffer_ops = {
	.RequestBuffer = mme_request_buffer,
	.ReleaseBuffer = mme_release_buffer,
};

struct mm_parser_plugin_info {
	const char *type;
	const char *libname;
	uint32_t flag_insert_startcode;
};

const struct mm_parser_plugin_info ParserPlugins[] = {
	{"avi",  "/usr/lib/imx-mm/parser/lib_avi_parser_arm_elinux.so",   0},
	{"mkv",  "/usr/lib/imx-mm/parser/lib_mkv_parser_arm_elinux.so",   0},
	{"webm", "/usr/lib/imx-mm/parser/lib_mkv_parser_arm_elinux.so",   0},
	{"mp4",  "/usr/lib/imx-mm/parser/lib_mp4_parser_arm_elinux.so",   0},
	{"mov",  "/usr/lib/imx-mm/parser/lib_mp4_parser_arm_elinux.so",   0},
	{"3gp",  "/usr/lib/imx-mm/parser/lib_mp4_parser_arm_elinux.so",   0},
	{"mpg",  "/usr/lib/imx-mm/parser/lib_mpg2_parser_arm_elinux.so",  0},
	{"m2ts", "/usr/lib/imx-mm/parser/lib_mpg2_parser_arm_elinux.so",  0},
	{"ts",   "/usr/lib/imx-mm/parser/lib_mpg2_parser_arm_elinux.so",  0},
	{"vob",  "/usr/lib/imx-mm/parser/lib_mpg2_parser_arm_elinux.so",  0},
	{"m2v",  "/usr/lib/imx-mm/parser/lib_mpg2_parser_arm_elinux.so",  0},
	{"flv",  "/usr/lib/imx-mm/parser/lib_flv_parser_arm_elinux.so",   0},
	{"rm",   "/usr/lib/imx-mm/parser/lib_rm_parser_arm_elinux.so",    1},
	{"rmvb", "/usr/lib/imx-mm/parser/lib_rm_parser_arm_elinux.so",    1},
	{NULL, NULL, 0},
};


const struct mm_parser_plugin_info *mme_get_parser_plugin(const char *filename)
{
	char *suffix = strrchr(filename, '.');
	const struct mm_parser_plugin_info *plugin;

	if (!suffix)
		return NULL;

	plugin = &ParserPlugins[0];
	while (plugin->type) {
		if (!strcasecmp(plugin->type, suffix + 1))
			return plugin;
		plugin++;
	}

	return NULL;
}

int mme_load_parser(struct mm_extractor_test_t *mme)
{
	const struct mm_parser_plugin_info *plugin;
	struct mm_parser_interface *parser;
	int ret = RET_OK;

	if (!mme || !mme->filename)
		return -RET_E_NULL_POINTER;

	plugin = mme_get_parser_plugin(mme->filename);
	if (!plugin || !plugin->libname) {
		PITCHER_ERR("can't find parser plugin for %s\n", mme->filename);
		return -RET_E_NOT_FOUND;
	}

	parser = &mme->parser;
	parser->library = dlopen(plugin->libname, RTLD_NOW | RTLD_LOCAL);
	if (!parser->library) {
		PITCHER_ERR("fail to load parser %s : %s\n", plugin->libname, dlerror());
		return -RET_E_OPEN;
	}

	parser->queryInterface = dlsym(mme->parser.library, "FslParserQueryInterface");
	if (!parser->queryInterface) {
		PITCHER_ERR("fail to find query interface\n");
		ret = -RET_E_NOT_FOUND;
		goto exit;
	}

	LOAD_FUNC(parser, PARSER_API_GET_VERSION_INFO, getVersionInfo);
	LOAD_FUNC(parser, PARSER_API_CREATE_PARSER, createParser);
	LOAD_FUNC(parser, PARSER_API_CREATE_PARSER2, createParser2);
	LOAD_FUNC(parser, PARSER_API_DELETE_PARSER, deleteParser);
	LOAD_FUNC(parser, PARSER_API_IS_MOVIE_SEEKABLE, isSeekable);
	LOAD_FUNC(parser, PARSER_API_GET_MOVIE_DURATION, getMovieDuration);
	LOAD_FUNC(parser, PARSER_API_GET_USER_DATA, getUserData);
	LOAD_FUNC(parser, PARSER_API_GET_NUM_TRACKS, getNumTracks);
	LOAD_FUNC(parser, PARSER_API_GET_TRACK_TYPE, getTrackType);
	LOAD_FUNC(parser, PARSER_API_GET_DECODER_SPECIFIC_INFO, getDecoderSpecificInfo);
	LOAD_FUNC(parser, PARSER_API_GET_LANGUAGE, getLanguage);
	LOAD_FUNC(parser, PARSER_API_GET_TRACK_DURATION, getTrackDuration);
	LOAD_FUNC(parser, PARSER_API_GET_BITRATE, getBitRate);
	LOAD_FUNC(parser, PARSER_API_GET_VIDEO_FRAME_WIDTH, getVideoFrameWidth);
	LOAD_FUNC(parser, PARSER_API_GET_VIDEO_FRAME_HEIGHT, getVideoFrameHeight);
	LOAD_FUNC(parser, PARSER_API_GET_VIDEO_FRAME_RATE, getVideoFrameRate);
	//LOAD_FUNC(parser, PARSER_API_GET_VIDEO_FRAME_COUNT, getVideoFrameCount);
	LOAD_FUNC(parser, PARSER_API_GET_READ_MODE, getReadMode);
	LOAD_FUNC(parser, PARSER_API_SET_READ_MODE, setReadMode);
	LOAD_FUNC(parser, PARSER_API_ENABLE_TRACK, enableTrack);
	LOAD_FUNC(parser, PARSER_API_GET_NEXT_SAMPLE, getNextSample);
	LOAD_FUNC(parser, PARSER_API_GET_NEXT_SYNC_SAMPLE, getNextSyncSample);
	LOAD_FUNC(parser, PARSER_API_GET_FILE_NEXT_SAMPLE, getFileNextSample);
	LOAD_FUNC(parser, PARSER_API_GET_FILE_NEXT_SYNC_SAMPLE, getFileNextSyncSample);
	LOAD_FUNC(parser, PARSER_API_SEEK, seek);
	LOAD_FUNC(parser, PARSER_API_INITIALIZE_INDEX, initializeIndex);
	LOAD_FUNC(parser, PARSER_API_IMPORT_INDEX, importIndex);
	LOAD_FUNC(parser, PARSER_API_EXPORT_INDEX, exportIndex);

	if (get_platform_type() == IMX_8X && plugin->flag_insert_startcode)
		mme->flag = FLAG_VIDEO_INSERT_HEADER;
	else
		mme->flag = 0;

	PITCHER_LOG("%s\n", parser->getVersionInfo());
	ret = RET_OK;
exit:
	if (ret != RET_OK)
		SAFE_RELEASE(parser->library, dlclose);

	return ret;
}

void mme_unload_parser(struct mm_extractor_test_t *mme)
{
	struct mm_parser_interface *parser;

	if (!mme || !mme->parser.library)
		return;

	parser = &mme->parser;
	SAFE_RELEASE(parser->library, dlclose);
	memset(parser, 0, sizeof(*parser));
}

int mme_init_video(struct mm_extractor_test_t *mme)
{
	struct mm_parser_interface *parser;
	uint32 flag = FLAG_H264_NO_CONVERT | FLAG_OUTPUT_PTS;
	uint32 track_num;
	uint32 media_type;
	uint32 decoder_type;
	uint32 sub_type;
	uint32 index;
	uint64 seekpos = 0;
	uint8 *decoderSpecificInfo = NULL;
	uint32 decoderSpecificInfoSize = 0;
	int ret = RET_OK;

	if (!mme || !mme->parser.library)
		return -RET_E_NULL_POINTER;

	parser = &mme->parser;
	flag |= FILE_FLAG_READ_IN_SEQUENCE;
	flag |= mme->flag;

	if (flag & FLAG_VIDEO_INSERT_HEADER)
		PITCHER_LOG("enable parser insert video startcode\n");

	call_vop(ret, 1, parser, createParser2, flag,
		 &mme_file_ops, &mme_memory_ops, &mme_buffer_ops, mme, &mme->handle);
	call_vop(ret, 0, parser, initializeIndex, mme->handle);
	mme->read_mode = PARSER_READ_MODE_TRACK_BASED;
	call_vop(ret, 0, parser, setReadMode, mme->handle, mme->read_mode);
	if (ret) {
		mme->read_mode = PARSER_READ_MODE_FILE_BASED;
		call_vop(ret, 1, parser, setReadMode, mme->handle, mme->read_mode);
	}

	call_vop(ret, 1, parser, getNumTracks, mme->handle, &track_num);
	call_vop(ret, 1, parser, isSeekable, mme->handle, &mme->seekable);
	call_vop(ret, 1, parser, getMovieDuration, mme->handle, &mme->duration);
	for (index = 0; index < track_num; index++) {
		call_vop(ret, 0, parser, getTrackType, mme->handle, index,
			 &media_type, &decoder_type, &sub_type);
		if (media_type == MEDIA_VIDEO) {
			mme->track_index = index;
			mme->decoder_type = decoder_type;
			mme->sub_type = sub_type;
			break;
		}
	}

	if (mme->track_index < 0) {
		PITCHER_ERR("can't find video track from %s\n", mme->filename);
		ret = -RET_E_NOT_FOUND;
		goto exit;
	}

	call_vop(ret, 1, parser, getTrackDuration, mme->handle, mme->track_index, &mme->duration);
	call_vop(ret, 1, parser, getBitRate, mme->handle, mme->track_index, &mme->bitrate);
	call_vop(ret, 1, parser, getVideoFrameRate, mme->handle, mme->track_index, &mme->rate, &mme->scale);
	call_vop(ret, 1, parser, getVideoFrameWidth, mme->handle, mme->track_index, &mme->width);
	call_vop(ret, 1, parser, getVideoFrameHeight, mme->handle, mme->track_index, &mme->height);
	call_vop(ret, 0, parser, getDecoderSpecificInfo, mme->handle, mme->track_index, &decoderSpecificInfo, &decoderSpecificInfoSize);
	if (decoderSpecificInfo && decoderSpecificInfoSize) {
		mme->codec_data = decoderSpecificInfo;
		mme->codec_data_size = decoderSpecificInfoSize;
	}

	PITCHER_LOG("mm parse, %s mode, track %d type %d-%d;duration %lld; size %d x %d; fps %.2f; bitrate %d\n",
		    mme->read_mode == PARSER_READ_MODE_TRACK_BASED ? "track" : "file",
		    mme->track_index,
		    mme->decoder_type, mme->sub_type,
		    mme->duration, mme->width, mme->height,
		    mme->scale ? mme->rate * 1.0 / mme->scale : 0.0,
		    mme->bitrate);

	call_vop(ret, 1, parser, enableTrack, mme->handle, mme->track_index, 1);
	call_vop(ret, 0, parser, seek, mme->handle, mme->track_index, &seekpos, SEEK_FLAG_NO_LATER);
exit:
	return ret;
}

void mme_uninit_video(struct mm_extractor_test_t *mme)
{
	struct mm_parser_interface *parser;

	if (!mme || !mme->parser.library)
		return;
	if (!mme->handle)
		return;

	parser = &mme->parser;
	call_void_op(parser, enableTrack, mme->handle, mme->track_index, 0);
	call_void_op(parser, deleteParser, mme->handle);
	mme->handle = NULL;
}

struct mme_pixel_format_mapping {
	uint32_t decoder_type;
	uint32_t sub_type;
	uint32_t pixelformat;
};

static struct mme_pixel_format_mapping mme_pixel_format_mappings[] = {
	{VIDEO_H264,                   0, PIX_FMT_H264},
	{VIDEO_HEVC,                   0, PIX_FMT_H265},
	{VIDEO_AVS,                    0, PIX_FMT_AVS},
	{VIDEO_MPEG2,                  0, PIX_FMT_MPEG2},
	{VIDEO_MPEG4,                  0, PIX_FMT_MPEG4},
	{VIDEO_MS_MPEG4,               0, PIX_FMT_MPEG4},
	{VIDEO_H263,                   0, PIX_FMT_H263},
	{VIDEO_XVID,                   0, PIX_FMT_XVID},
	{VIDEO_MJPG,                   0, PIX_FMT_JPEG},
	{VIDEO_JPEG,                   0, PIX_FMT_JPEG},
	{VIDEO_SORENSON,               0, PIX_FMT_SPK},
	{VIDEO_SORENSON_H263,          0, PIX_FMT_SPK},
	{VIDEO_ON2_VP,         VIDEO_VP8, PIX_FMT_VP8},
	{VIDEO_ON2_VP,         VIDEO_VP9, PIX_FMT_VP9},
	{VIDEO_REAL,                   3, PIX_FMT_RV30},
	{VIDEO_REAL,                   4, PIX_FMT_RV40},
	/*{VIDEO_AV1,                  0, PIX_FMT_AV1},*/
	{VIDEO_TYPE_UNKNOWN,           0, PIX_FMT_NONE}
};

uint32_t mme_get_pixel_foramt(uint32 decoder_type, uint32 sub_type)
{
	struct mme_pixel_format_mapping *fmt = &mme_pixel_format_mappings[0];

	while (fmt->decoder_type != VIDEO_TYPE_UNKNOWN) {
		if (fmt->decoder_type == decoder_type) {
			if (!fmt->sub_type)
				return fmt->pixelformat;
			if (fmt->sub_type == sub_type)
				return fmt->pixelformat;
		}
		fmt++;
	}

	return PIX_FMT_NONE;
}

int mme_get_next_sample(struct mm_extractor_test_t *mme)
{
	struct mm_parser_interface *parser;
	int ret = -RET_E_NOT_FOUND;

	if (!mme || !mme->parser.library)
		return -RET_E_NULL_POINTER;

	parser = &mme->parser;
	while (1) {
		uint8 *pdata = NULL;
		void *buffer_context = NULL;
		uint32 size = 0;
		uint64 timestamp = 0;
		uint64 usDuration = 0;
		uint32 flag = 0;

		if (mme->read_mode == PARSER_READ_MODE_TRACK_BASED) {
			ret = parser->getNextSample(mme->handle, mme->track_index,
						    &pdata, &buffer_context, &size,
						    &timestamp, &usDuration, &flag);
		} else {
			uint32 track_num_got = mme->track_index;

			ret = parser->getFileNextSample(mme->handle, &track_num_got,
							&pdata, &buffer_context, &size,
							&timestamp, &usDuration, &flag);
			if (track_num_got != mme->track_index)
				continue;
		}
		if (ret == PARSER_NOT_READY) {
			PITCHER_ERR("parser not ready\n");
			break;
		}
		if (pdata && size)
			mme->buffer->planes[0].bytesused += size;
		if (!(flag & FLAG_SAMPLE_NOT_FINISHED)) {
			if (mme->buffer->planes[0].bytesused) {
				ret = RET_OK;
				mme->frame_count++;
			}
		} else {
			ret = -RET_E_NOT_READY;
		}
		if (ret == PARSER_EOS) {
			PITCHER_LOG("parse eos\n");
			mme->end = true;
			break;
		}
		if (ret == RET_OK)
			break;
	}

	return ret;
}

int get_mme_chnno(struct test_node *node)
{
	struct mm_extractor_test_t *mme;

	if (!node)
		return -RET_E_NULL_POINTER;

	mme = container_of(node, struct mm_extractor_test_t, node);
	return mme->chnno;
}

int mme_recycle_buffer(struct pitcher_buffer *buffer, void *arg, int *del)
{
	struct mm_extractor_test_t *mme = arg;
	int is_end = false;

	if (!mme)
		return -RET_E_NULL_POINTER;

	if (pitcher_is_active(mme->chnno) && !mme->end)
		pitcher_put_buffer_idle(mme->chnno, buffer);
	else
		is_end = true;

	if (del)
		*del = is_end;

	return RET_OK;
}

struct pitcher_buffer *mme_alloc_buffer(void *arg)
{
	struct mm_extractor_test_t *mme = arg;
	struct pitcher_buffer_desc desc;

	if (!mme)
		return NULL;

	memset(&desc, 0, sizeof(desc));
	desc.plane_count = 1;
	desc.plane_size[0] = mme->sizeimage;
	desc.init_plane = pitcher_alloc_plane;
	desc.uninit_plane = pitcher_free_plane;
	desc.recycle = mme_recycle_buffer;
	desc.arg = mme;

	return pitcher_new_buffer(&desc);
}

int mme_checkready(void *arg, int *is_end)
{
	struct mm_extractor_test_t *mme = arg;

	if (!mme)
		return false;

	if (is_force_exit())
		mme->end = true;
	if (is_end)
		*is_end = mme->end;
	if (mme->end)
		return false;
	if (pitcher_poll_idle_buffer(mme->chnno))
		return true;

	return false;
}

int mme_avcc2annexb_codecdata(struct mm_extractor_test_t *mme, struct pitcher_buffer *buffer)
{
	uint8_t num_nal;
	int sps_done = 0;
	uint16_t nal_size;
	uint32_t offset = 0;
	uint8_t *psrc;
	uint8_t *pdst;
	uint32_t bytesused = 0;
	uint8_t startcode[] = {0x0, 0x0, 0x0, 0x1};

	mme->length_size = (mme->codec_data[4] & 0x3) + 1;
	num_nal = mme->codec_data[5] & 0x1f;

	if (mme->length_size != 3 && mme->length_size != 4) {
		PITCHER_LOG("length_size %d is not supported\n", mme->length_size);
		return -RET_E_INVAL;
	}

	offset = 6;
	psrc = mme->codec_data + offset;
	pdst = buffer->planes[0].virt;

	memcpy(pdst + bytesused, startcode, sizeof(startcode));
	bytesused += sizeof(startcode);

	PITCHER_LOG("length_size %d, num sps %d\n", mme->length_size, num_nal);
	while (num_nal--) {
		nal_size = ((uint16_t)psrc[0] << 8) | psrc[1];
		if (nal_size + 2 + offset > mme->codec_data_size) {
			PITCHER_LOG("nal_size %d is out of range\n", nal_size);
			return -RET_E_INVAL;
		}
		memcpy(pdst + bytesused, psrc + 2, nal_size);
		bytesused += nal_size;
		offset += 2 + nal_size;
		psrc = mme->codec_data + offset;

		if (!num_nal && !sps_done++) {
			num_nal = psrc[0];
			PITCHER_LOG("num pps %d\n", num_nal);
			offset++;
			psrc = mme->codec_data + offset;

			memcpy(pdst + bytesused, startcode, sizeof(startcode));
			bytesused += sizeof(startcode);
		}
	}
	buffer->planes[0].bytesused = bytesused;

	return RET_OK;
}

int mme_hvcc2annexb_codecdata(struct mm_extractor_test_t *mme, struct pitcher_buffer *buffer)
{
	uint8_t num_array;
	uint32_t offset = 0;
	uint8_t *psrc;
	uint8_t *pdst;
	uint32_t bytesused = 0;
	uint8_t startcode[] = {0x0, 0x0, 0x0, 0x1};

	mme->length_size = (mme->codec_data[21] & 0x3) + 1;

	PITCHER_LOG("length_size %d\n", mme->length_size);
	if (mme->length_size != 3 && mme->length_size != 4) {
		PITCHER_LOG("length_size %d is not supported\n", mme->length_size);
		return -RET_E_INVAL;
	}
	num_array = mme->codec_data[22];

	offset = 23;
	psrc = mme->codec_data + offset;
	pdst = buffer->planes[0].virt;

	while (num_array--) {
		int cnt;

		if (offset + 5 > mme->codec_data_size) {
			PITCHER_LOG("hvcc codec data is out of range\n");
			return -RET_E_INVAL;
		}
		cnt = pitcher_bytestream_get_be(psrc + 1, 2);

		offset += 3;
		psrc = mme->codec_data + offset;
		for (int i = 0; i < cnt; i++) {
			uint16_t nal_size = pitcher_bytestream_get_be(psrc, 2);

			if (offset + 2 + nal_size > mme->codec_data_size) {
				PITCHER_LOG("nal_size %d is out of range\n", nal_size);
				return -RET_E_INVAL;
			}

			memcpy(pdst + bytesused, startcode, sizeof(startcode));
			bytesused += sizeof(startcode);
			memcpy(pdst + bytesused, psrc + 2, nal_size);
			bytesused += nal_size;

			offset += 2 + nal_size;
			psrc = mme->codec_data + offset;
		}
	}
	buffer->planes[0].bytesused = bytesused;

	return RET_OK;
}

void mme_mp4_to_annexb_frame(struct mm_extractor_test_t *mme, struct pitcher_buffer *buffer)
{
	uint8_t *pdata = buffer->planes[0].virt;
	uint32_t bytesused = buffer->planes[0].bytesused;
	uint32_t length;
	uint32_t consumed = 0;
	uint32_t num_nal = 0;

	while (consumed < bytesused) {
		length = pitcher_bytestream_get_be(pdata, mme->length_size);
		pitcher_bytestream_set_be(pdata, 0x1, mme->length_size);
		pdata += mme->length_size + length;
		consumed += mme->length_size + length;
		num_nal++;
	}
}

int mme_handle_frame(struct mm_extractor_test_t *mme, struct pitcher_buffer *buffer)
{
	int ret;

	if (mme->codec_data && mme->codec_data_size && !mme->codec_data_sent) {
		if (mme->codec_data_size > buffer->planes[0].size) {
			PITCHER_ERR("buffer size(%ld) is too small for codec data(%d)\n",
				    buffer->planes[0].size, mme->codec_data_size);
			mme->end = true;
			return -RET_E_INVAL;
		}
		PITCHER_LOG("codec data(%s): %8d, %02x %02x %02x %02x %02x %02x %02x %02x\n",
			    pitcher_get_format_name(mme->format.format),
			    mme->codec_data_size,
			    mme->codec_data[0], mme->codec_data[1],
			    mme->codec_data[2], mme->codec_data[3],
			    mme->codec_data[4], mme->codec_data[5],
			    mme->codec_data[6], mme->codec_data[7]);
		if (mme->format.format == PIX_FMT_H264) {
			mme->is_avcc = true;
			if (mme_avcc2annexb_codecdata(mme, buffer)) {
				PITCHER_ERR("avcc bitstream is not supported\n");
				mme->end = true;
				return -RET_E_INVAL;
			}
			mme->codec_data_sent = true;
			return RET_OK;
		} else if (mme->format.format == PIX_FMT_H265) {
			mme->is_hvcc = true;
			if (mme_hvcc2annexb_codecdata(mme, buffer)) {
				PITCHER_ERR("hvcc bitstream is not supported\n");
				mme->end = true;
				return -RET_E_INVAL;
			}
			mme->codec_data_sent = true;
			return RET_OK;
		}

		memcpy(buffer->planes[0].virt, mme->codec_data, mme->codec_data_size);
		buffer->planes[0].bytesused = mme->codec_data_size;
		mme->codec_data_sent = true;
		return RET_OK;
	}

	ret = mme_get_next_sample(mme);
	if (ret)
		return ret;


	if (mme->is_avcc || mme->is_hvcc)
		mme_mp4_to_annexb_frame(mme, buffer);

	return RET_OK;
}

int mme_run(void *arg, struct pitcher_buffer *pbuf)
{
	struct mm_extractor_test_t *mme = arg;
	struct pitcher_buffer *buffer;
	int ret = RET_OK;

	if (!mme)
		return -RET_E_INVAL;

	buffer = pitcher_get_idle_buffer(mme->chnno);
	if (!buffer)
		return -RET_E_NOT_READY;

	buffer->format = &mme->format;
	buffer->planes[0].bytesused = 0;
	mme->buffer = buffer;
	ret = mme_handle_frame(mme, buffer);
	mme->buffer = NULL;

	if (0) {
		uint8_t *data = buffer->planes[0].virt;

		PITCHER_LOG("[%8ld][%8ld] %02x %02x %02x %02x %02x %02x %02x %02x\n",
			mme->frame_count, buffer->planes[0].bytesused,
			data[0], data[1], data[2], data[3],
			data[4], data[5], data[6], data[7]);
	}
	if (ret == RET_OK)
		pitcher_push_back_output(mme->chnno, buffer);

	SAFE_RELEASE(buffer, pitcher_put_buffer);

	return ret;
}

int init_mme_node(struct test_node *node)
{
	struct mm_extractor_test_t *mme;
	int ret;

	if (!node)
		return -RET_E_NULL_POINTER;

	mme = container_of(node, struct mm_extractor_test_t, node);
	if (!mme->filename)
		return -RET_E_INVAL;

	//to be done
	ret = mme_load_parser(mme);
	if (ret != RET_OK)
		return ret;
	ret = mme_init_video(mme);
	if (ret != RET_OK)
		goto error;
	mme->node.pixelformat = mme_get_pixel_foramt(mme->decoder_type, mme->sub_type);
	mme->node.width = mme->width;
	mme->node.height = mme->height;
	PITCHER_LOG("parse %s\n", mme->filename);

	mme->format.format = mme->node.pixelformat;
	mme->format.width = mme->node.width;
	mme->format.height = mme->node.height;
	pitcher_get_pix_fmt_info(&mme->format, 0);

	mme->desc.fd = -1;
	mme->desc.check_ready = mme_checkready;
	mme->desc.runfunc = mme_run;
	mme->desc.buffer_count = 4;
	mme->desc.alloc_buffer = mme_alloc_buffer;
	snprintf(mme->desc.name, sizeof(mme->desc.name), "media.%s.%d", mme->filename, mme->node.key);
	ret = pitcher_register_chn(mme->node.context, &mme->desc, mme);
	if (ret < 0) {
		PITCHER_ERR("register mm parse fail\n");
		goto error;
	}
	mme->chnno = ret;

	return RET_OK;
error:
	mme_uninit_video(mme);
	mme_unload_parser(mme);
	return ret;
}

void free_mme_node(struct test_node *node)
{
	struct mm_extractor_test_t *mme;

	if (!node)
		return;

	mme = container_of(node, struct mm_extractor_test_t, node);
	PITCHER_LOG("mm parser frame count %ld\n", mme->frame_count);
	SAFE_CLOSE(mme->chnno, pitcher_unregister_chn);
	mme_uninit_video(mme);
	mme_unload_parser(mme);
	SAFE_RELEASE(mme, pitcher_free);
}

struct test_node *alloc_mm_extractor_node(void)
{
	struct mm_extractor_test_t *mme;

	mme = pitcher_calloc(1, sizeof(*mme));
	if (!mme)
		return NULL;

	mme->node.key = -1;
	mme->node.source = -1;
	mme->node.type = TEST_TYPE_MM_EXTRACTOR;
	mme->node.pixelformat = PIX_FMT_NONE;
	mme->sizeimage = 2 * 1024 * 1024;
	mme->track_index = -1;

	mme->node.get_source_chnno = get_mme_chnno;
	mme->node.init_node = init_mme_node;
	mme->node.free_node = free_mme_node;

	return &mme->node;
}
