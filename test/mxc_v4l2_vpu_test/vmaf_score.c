/*
 * Copyright(c) 2025 NXP. All rights reserved.
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
#include "pitcher/pitcher_def.h"
#include "pitcher/pitcher.h"
#include "pitcher/queue.h"
#include "pitcher/loadso.h"
#include "mxc_v4l2_vpu_enc.h"
#include "libvmaf/picture.h"
#include "libvmaf/libvmaf.h"

#define LIBVMAF_LIBRARY_PATH_DEFAULT	"/usr/lib/libvmaf.so"

struct score_component_t {
	int chnno;
	struct pitcher_unit_desc desc;
	unsigned long frame_count;
	Queue buffers;
	int enable;
	int end;
};

struct score_vmaf_interface {
	int (*vmaf_init)(VmafContext **vmaf, VmafConfiguration cfg);
	int (*vmaf_use_features_from_model)(VmafContext *vmaf, VmafModel *model);
	int (*vmaf_use_feature)(VmafContext *vmaf, const char *feature_name,
				VmafFeatureDictionary *opts_dict);
	int (*vmaf_read_pictures)(VmafContext *vmaf, VmafPicture *ref, VmafPicture *dist,
				  unsigned index);
	int (*vmaf_score_at_index)(VmafContext *vmaf, VmafModel *model, double *score,
				   unsigned index);
	int (*vmaf_feature_score_at_index)(VmafContext *vmaf, const char *feature_name,
				   double *score, unsigned index);
	int (*vmaf_score_pooled)(VmafContext *vmaf, VmafModel *model,
				 enum VmafPoolingMethod pool_method, double *score,
				 unsigned index_low, unsigned index_high);
	int (*vmaf_feature_score_pooled)(VmafContext *vmaf, const char *feature_name,
					 enum VmafPoolingMethod pool_method, double *score,
					 unsigned index_low, unsigned index_high);
	int (*vmaf_close)(VmafContext *vmaf);
	int (*vmaf_write_output)(VmafContext *vmaf, const char *output_path,
				 enum VmafOutputFormat fmt);
	const char *(*vmaf_version)(void);

	int (*vmaf_model_load)(VmafModel **model, VmafModelConfig *cfg,
			       const char *version);
	void (*vmaf_model_destroy)(VmafModel *model);

	int (*vmaf_picture_alloc)(VmafPicture *pic, enum VmafPixelFormat pix_fmt,
				  unsigned bpc, unsigned w, unsigned h);
	int (*vmaf_picture_unref)(VmafPicture *pic);
};

struct score_t {
	struct test_node node;
	struct pix_fmt_info format;
	struct score_component_t reference;
	struct score_component_t distorted;
	int ref_key;
	int ref_chn;
	unsigned long frame_count;
	uint32_t target_format;
	uint32_t target_width;
	uint32_t target_height;
	int end;

	double vmaf_score;
	double psnr_y_score;
	double psnr_cb_score;
	double psnr_cr_score;

	const char *output_path;

	const char *dll_path;
	void *dll_handle;
	struct score_vmaf_interface iface;

	VmafConfiguration cfg;
	VmafContext *vmaf;
	VmafModel *model;
	VmafModelConfig modelcfg;
	const char *modelversion;
	const char *feature_name;
};

struct mxc_vpu_test_option score_options[] = {
	{"key",  1, "--key <key>\n\t\t\tassign key number"},
	{"source", 1, "--source <key no>\n\t\t\tset source key number"},
	{"ref", 1, "--ref <key no>\n\t\t\tset reference key number"},
	{"libvmaf", 1, "--libvmaf <the path of vmaf library>\n\t\t\tset libvmaf path"},
	{"output", 1, "--output <path>\n\t\t\tset output file (as XML)"},
	{NULL, 0, NULL},
};

static int score_destroy_vmaf(struct score_t *score)
{
	SAFE_RELEASE(score->model, score->iface.vmaf_model_destroy);
	SAFE_RELEASE(score->vmaf, score->iface.vmaf_close);

	return 0;
}

static int score_init_vmaf(struct score_t *score)
{
	int ret;

	ret = score->iface.vmaf_init(&score->vmaf, score->cfg);
	if (ret) {
		PITCHER_ERR("init vmaf context failed, ret = %d\n", ret);
		return -RET_E_INVAL;
	}

	ret = score->iface.vmaf_model_load(&score->model, &score->modelcfg, score->modelversion);
	if (ret) {
		PITCHER_ERR("load vmaf model version %s failed, ret = %d\n",
			    score->modelversion, ret);

		goto error;
	}

	ret = score->iface.vmaf_use_features_from_model(score->vmaf, score->model);
	if (ret) {
		PITCHER_ERR("fail to use features from model\n");
		goto error;
	}

	ret = score->iface.vmaf_use_feature(score->vmaf, score->feature_name, NULL);
	if (ret) {
		PITCHER_ERR("use feature %s failed, ret = %d\n", score->feature_name, ret);
		goto error;
	}

	return RET_OK;
error:
	score_destroy_vmaf(score);
	return -RET_E_INVAL;
}

static int score_fetch_picture(struct score_t *score, struct pitcher_buffer *buf, VmafPicture *pic)
{
	struct pix_fmt_info *format = buf->format ? buf->format : &score->format;
	const struct pixel_format_desc *desc = format->desc;
	struct pitcher_buf_ref splane;
	int w, h, line;
	int planes_line;
	unsigned long offset;
	int i, j;
	int ret;

	ret = score->iface.vmaf_picture_alloc(pic,
					      VMAF_PIX_FMT_YUV420P,
					      8,
					      score->target_width,
					      score->target_height);
	if (ret) {
		PITCHER_ERR("Fail to allocate picture\n");
		return -RET_E_NO_MEMORY;
	}

	for (i = 0; i < 3; i++) {
		uint32_t left;
		uint32_t top;
		uint32_t x_offset;
		uint8_t *pic_data = pic->data[i];

		pitcher_get_buffer_plane(buf, i, &splane);
		w = score->target_width;
		h = score->target_height;

		w = ALIGN(w, 1 << desc->log2_chroma_w);
		h = ALIGN(h, 1 << desc->log2_chroma_h);
		if (i) {
			w >>= desc->log2_chroma_w;
			h >>= desc->log2_chroma_h;
		}
		line = ALIGN(w * desc->comp[i].bpp, 8) >> 3;
		if (buf->crop) {
			left = buf->crop->left;
			top = buf->crop->top;
		} else {
			left = 0;
			top = 0;
		}
		if (i) {
			left >>= desc->log2_chroma_w;
			top >>= desc->log2_chroma_h;
		}

		x_offset = ALIGN(left * desc->comp[i].bpp, 8) >> 3;
		planes_line = format->planes[i].line;
		offset = planes_line * top;
		for (j = 0; j < h; j++) {
			memcpy(pic_data, (uint8_t *)splane.virt + offset + x_offset, line);
			pic_data += pic->stride[i];
			offset += planes_line;
		}
	}

	return RET_OK;
}

static int score_read_pictures(struct score_t *score,
			       struct pitcher_buffer *dist, struct pitcher_buffer *ref)
{
	VmafPicture pic_dict;
	VmafPicture pic_ref;
	int ret;

	ret = score_fetch_picture(score, ref, &pic_ref);
	if (ret < 0) {
		PITCHER_ERR("fetch reference picture failed\n");
		return ret;
	}
	ret = score_fetch_picture(score, dist, &pic_dict);
	if (ret < 0) {
		score->iface.vmaf_picture_unref(&pic_ref);
		PITCHER_ERR("fetch distorted picture failed\n");
		return ret;
	}

	ret = score->iface.vmaf_read_pictures(score->vmaf, &pic_ref, &pic_dict, score->frame_count);
	if (ret) {
		PITCHER_ERR("Problem reading pictures\n");
	}

	return RET_OK;
}

static int score_finalize(struct score_t *score)
{
	int ret;

	ret = score->iface.vmaf_read_pictures(score->vmaf, NULL, NULL, 0);
	if (ret) {
		PITCHER_ERR("Fail to flush vmaf context\n");
		return ret;
	}

	ret = score->iface.vmaf_score_pooled(score->vmaf, score->model, VMAF_POOL_METHOD_MEAN,
					     &score->vmaf_score, 0, score->frame_count - 1);
	if (ret < 0) {
		PITCHER_ERR("Failed to pool vmaf score, ret = %d\n", ret);
		return -RET_E_INVAL;
	}

	ret = score->iface.vmaf_feature_score_pooled(score->vmaf, "psnr_y",
						     VMAF_POOL_METHOD_MEAN,
						     &score->psnr_y_score,
						     0, score->frame_count - 1);
	if (ret < 0)
		PITCHER_ERR("Failed to pool psnr_y score, ret = %d\n", ret);

	ret = score->iface.vmaf_feature_score_pooled(score->vmaf, "psnr_cb",
						     VMAF_POOL_METHOD_MEAN,
						     &score->psnr_cb_score,
						     0, score->frame_count - 1);
	if (ret < 0)
		PITCHER_ERR("Failed to pool psnr_y score, ret = %d\n", ret);

	ret = score->iface.vmaf_feature_score_pooled(score->vmaf, "psnr_cr",
						     VMAF_POOL_METHOD_MEAN,
						     &score->psnr_cr_score,
						     0, score->frame_count - 1);
	if (ret < 0)
		PITCHER_ERR("Failed to pool psnr_y score, ret = %d\n", ret);

	if (score->output_path)
		score->iface.vmaf_write_output(score->vmaf,
					       score->output_path,
					       VMAF_OUTPUT_FORMAT_XML);

	return RET_OK;
}

static int score_precheck_picture(struct score_t *score,
				  struct pitcher_buffer *ref, struct pitcher_buffer *dist)
{
	uint32_t dist_w, dist_h;
	uint32_t ref_w, ref_h;

	if (ref->crop && ref->crop->width && ref->crop->height) {
		ref_w = ref->crop->width;
		ref_h = ref->crop->height;
	} else if (ref->format) {
		ref_w = ref->format->width;
		ref_h = ref->format->height;
	} else {
		ref_w = score->format.width;
		ref_h = score->format.height;
	}

	if (dist->crop && dist->crop->width && dist->crop->height) {
		dist_w = dist->crop->width;
		dist_h = dist->crop->height;
	} else if (dist->format) {
		dist_w = dist->format->width;
		dist_h = dist->format->height;
	} else {
		dist_w = score->format.width;
		dist_h = score->format.height;
	}

	if (ref_w != dist_w || ref_h != dist_h)
		return -RET_E_NOT_MATCH;

	if (!score->target_width || !score->target_height) {
		score->target_width = dist_w;
		score->target_height = dist_h;
	}

	if (score->target_width != dist_w || score->target_height != dist_h)
		return -RET_E_NOT_MATCH;

	return RET_OK;
}

static int score_handle_picture(struct score_t *score)
{
	struct pitcher_buffer *dist, *ref;
	int ret;

	if (score->end)
		return -RET_E_NOT_READY;
	if ((pitcher_queue_is_empty(score->distorted.buffers) && !score->distorted.end) ||
	    (pitcher_queue_is_empty(score->reference.buffers) && !score->reference.end))
		return -RET_E_NOT_READY;

	dist = pitcher_queue_pop_buffer(score->distorted.buffers);
	ref = pitcher_queue_pop_buffer(score->reference.buffers);

	if (!score->distorted.end && !score->reference.end &&
	    dist && ref && dist->planes[0].bytesused && ref->planes[0].bytesused) {
		ret = score_precheck_picture(score, ref, dist);
		if (ret) {
			SAFE_RELEASE(dist, pitcher_put_buffer);
			SAFE_RELEASE(ref, pitcher_put_buffer);
			PITCHER_ERR("distorted and reference pictures are mismatch\n");
			return ret;
		}

		score_read_pictures(score, dist, ref);
		score->frame_count++;
	}

	if (dist && dist->flags & PITCHER_BUFFER_FLAG_LAST)
		score->distorted.end = true;
	if (ref && ref->flags & PITCHER_BUFFER_FLAG_LAST)
		score->reference.end = true;

	SAFE_RELEASE(dist, pitcher_put_buffer);
	SAFE_RELEASE(ref, pitcher_put_buffer);

	return RET_OK;
}

static int score_start(struct score_t *score, struct score_component_t *comp)
{
	int ret;

	if (!comp->buffers) {
		comp->buffers = pitcher_init_queue();
		if (!comp->buffers)
			return -RET_E_NO_MEMORY;
	}

	comp->enable = true;
	if (score->distorted.enable && score->reference.enable) {
		ret = score_init_vmaf(score);
		if (ret < 0) {
			SAFE_RELEASE(comp->buffers, pitcher_destroy_queue);
			comp->enable = false;
			return ret;
		}
	}

	return RET_OK;
}

int score_checkready(struct score_t *score, int *is_end, struct score_component_t *comp)
{
	if (!score)
		return -RET_E_INVAL;

	if (is_force_exit())
		score->end = true;
	if (is_source_end(comp->chnno) && pitcher_queue_is_empty(comp->buffers))
		comp->end = true;
	if (score->distorted.end && score->reference.end)
		score->end = true;
	if (is_end)
		*is_end = score->end;

	return true;
}

static int score_run(struct score_t *score,
		     struct score_component_t *comp, struct pitcher_buffer *buffer)
{
	if (buffer->format && buffer->format->format != score->target_format)
		return -RET_E_NOT_SUPPORT;
	if (!comp->enable)
		return RET_OK;

	pitcher_queue_push_back(comp->buffers, (unsigned long)pitcher_get_buffer(buffer));
	if (buffer->count && buffer->planes && buffer->planes[0].bytesused)
		comp->frame_count++;
	score_handle_picture(score);

	return RET_OK;
}

static int score_stop(struct score_t *score, struct score_component_t *comp)
{
	if (score->distorted.enable && score->reference.enable) {
		if (score->frame_count > 0)
			score_finalize(score);
		score_destroy_vmaf(score);
	}

	pitcher_queue_clear(comp->buffers, pitcher_queue_clear_buffer, NULL);
	comp->enable = false;

	return RET_OK;
}

int score_distorted_start(void *arg)
{
	struct score_t *score = arg;

	if (!score)
		return -RET_E_INVAL;

	return score_start(score, &score->distorted);
}

int score_distorted_checkready(void *arg, int *is_end)
{
	struct score_t *score = arg;

	if (!score)
		return -RET_E_INVAL;

	return score_checkready(score, is_end, &score->distorted);
}

int score_distorted_run(void *arg, struct pitcher_buffer *buffer)
{
	struct score_t *score = arg;

	if (!score)
		return -RET_E_INVAL;
	if (!buffer)
		return -RET_E_NOT_READY;

	return score_run(score, &score->distorted, buffer);
}

int score_distorted_stop(void *arg)
{
	struct score_t *score = arg;

	if (!score)
		return -RET_E_INVAL;

	return score_stop(score, &score->distorted);
}

int score_reference_start(void *arg)
{
	struct score_t *score = arg;

	if (!score)
		return -RET_E_INVAL;

	return score_start(score, &score->reference);
}

int score_reference_checkready(void *arg, int *is_end)
{
	struct score_t *score = arg;

	if (!score)
		return -RET_E_INVAL;

	return score_checkready(score, is_end, &score->reference);
}

int score_reference_run(void *arg, struct pitcher_buffer *buffer)
{
	struct score_t *score = arg;

	if (!score)
		return -RET_E_INVAL;
	if (!buffer)
		return -RET_E_NOT_READY;

	return score_run(score, &score->reference, buffer);
}

int score_reference_stop(void *arg)
{
	struct score_t *score = arg;

	if (!score)
		return -RET_E_INVAL;

	return score_stop(score, &score->reference);
}

#define LOAD_INTERFACE(score, NAME)		\
		score->iface.NAME = pitcher_load_function(score->dll_handle, #NAME); \
		if (!score->iface.NAME) { \
			PITCHER_ERR("Fail to load vmaf interface %s\n", #NAME); \
			return -RET_E_NOSYS; \
		}

static int score_load_vmaf_interfaces(struct score_t *score)
{
	LOAD_INTERFACE(score, vmaf_init);
	LOAD_INTERFACE(score, vmaf_use_features_from_model);
	LOAD_INTERFACE(score, vmaf_use_feature);
	LOAD_INTERFACE(score, vmaf_read_pictures);
	LOAD_INTERFACE(score, vmaf_score_at_index);
	LOAD_INTERFACE(score, vmaf_feature_score_at_index);
	LOAD_INTERFACE(score, vmaf_score_pooled);
	LOAD_INTERFACE(score, vmaf_feature_score_pooled);
	LOAD_INTERFACE(score, vmaf_close);
	LOAD_INTERFACE(score, vmaf_write_output);
	LOAD_INTERFACE(score, vmaf_version);
	LOAD_INTERFACE(score, vmaf_model_load);
	LOAD_INTERFACE(score, vmaf_model_destroy);
	LOAD_INTERFACE(score, vmaf_picture_alloc);
	LOAD_INTERFACE(score, vmaf_picture_unref);

	return RET_OK;
}

int check_score_reference(struct test_node *node)
{
	struct test_node *ref_node;
	struct score_t *score;
	int ret;

	if (!node)
		return -RET_E_NULL_POINTER;

	score = container_of(node, struct score_t, node);
	if (score->reference.chnno >= 0)
		return RET_OK;

	ref_node = get_test_node(score->ref_key);
	if (!ref_node) {
		score->reference.end = true;
		return -RET_E_NOT_FOUND;
	}
	if (ref_node->pixelformat != score->target_format) {
		PITCHER_ERR("the format of reference and distorted should be same\n");
		return -RET_E_INVAL;
	}
	score->ref_chn = ref_node->get_source_chnno(ref_node);
	if (score->ref_chn < 0) {
		PITCHER_ERR("the reference chn is not exist\n");
		return -RET_E_NOT_FOUND;
	}

	//register reference chn
	score->reference.desc.fd = -1;
	score->reference.desc.start = score_reference_start;
	score->reference.desc.check_ready = score_reference_checkready;
	score->reference.desc.runfunc = score_reference_run;
	score->reference.desc.stop = score_reference_stop;
	snprintf(score->reference.desc.name, sizeof(score->reference.desc.name),
		 "vmaf.reference.%d", score->node.key);
	score->reference.chnno = pitcher_register_chn(score->node.context,
						      &score->reference.desc, score);
	if (score->reference.chnno < 0) {
		PITCHER_ERR("Fail to register vmaf reference chn\n");
		return score->reference.chnno;
	}
	PITCHER_LOG("score reference chnno %d\n", score->reference.chnno);

	ret = pitcher_connect(score->ref_chn, score->reference.chnno);
	if (ret < 0) {
		PITCHER_ERR("Fail to connect vmaf to reference\n");
		SAFE_CLOSE(score->reference.chnno, pitcher_unregister_chn);
		return ret;
	}

	return RET_OK;
}

int init_score_node(struct test_node *node)
{
	struct test_node *src_node;
	struct score_t *score;
	int ret;

	if (!node)
		return -RET_E_NULL_POINTER;

	score = container_of(node, struct score_t, node);

	score->dll_handle = pitcher_load_object(score->dll_path);
	if (!score->dll_handle) {
		PITCHER_ERR("load libvmaf (%s) failed\n", score->dll_path);
		return RET_E_INVAL;
	}

	ret = score_load_vmaf_interfaces(score);
	if (ret < 0) {
		SAFE_RELEASE(score->dll_handle, pitcher_unload_object);
		return ret;
	}

	score->cfg.log_level = VMAF_LOG_LEVEL_INFO;
	score->cfg.n_threads = 4;
	score->cfg.n_subsample = 0;
	score->cfg.cpumask = 0;
	score->cfg.gpumask = 0;
	score->modelversion = "vmaf_v0.6.1";
	score->feature_name = "psnr";

	src_node = get_test_node(score->node.source);
	if (!src_node || src_node->get_source_chnno(src_node) < 0)
		return -1;

	//register distorted chn
	score->distorted.desc.fd = -1;
	score->distorted.desc.start = score_distorted_start;
	score->distorted.desc.check_ready = score_distorted_checkready;
	score->distorted.desc.runfunc = score_distorted_run;
	score->distorted.desc.stop = score_distorted_stop;
	snprintf(score->distorted.desc.name, sizeof(score->distorted.desc.name),
		 "vmaf.distorted.%d", score->node.key);

	score->distorted.chnno = pitcher_register_chn(score->node.context,
						      &score->distorted.desc, score);

	PITCHER_LOG("score distorted chnno %d\n", score->distorted.chnno);

	check_score_reference(node);

	return RET_OK;
}

int get_score_distorted_chnno(struct test_node *node)
{
	struct score_t *score;

	if (!node)
		return -RET_E_NULL_POINTER;

	score = container_of(node, struct score_t, node);
	return score->distorted.chnno;
}

void free_score_node(struct test_node *node)
{
	struct score_t *score;

	if (!node)
		return;

	score = container_of(node, struct score_t, node);

	PITCHER_LOG("VMAF version: %s, %s, %ld frames, psnr(y/cb/cr): %f/%f/%f, vmaf score %f\n",
		    score->iface.vmaf_version(),
		    score->modelversion,
		    score->frame_count,
		    score->psnr_y_score, score->psnr_cb_score, score->psnr_cr_score,
		    score->vmaf_score);

	if (score->ref_chn >= 0 && score->reference.chnno >= 0)
		pitcher_disconnect(score->ref_chn, score->reference.chnno);

	if (score->distorted.buffers)
		pitcher_queue_clear(score->distorted.buffers, pitcher_queue_clear_buffer, NULL);
	if (score->reference.buffers)
		pitcher_queue_clear(score->reference.buffers, pitcher_queue_clear_buffer, NULL);
	SAFE_RELEASE(score->distorted.buffers, pitcher_destroy_queue);
	SAFE_RELEASE(score->reference.buffers, pitcher_destroy_queue);

	SAFE_CLOSE(score->distorted.chnno, pitcher_unregister_chn);
	SAFE_CLOSE(score->reference.chnno, pitcher_unregister_chn);

	SAFE_RELEASE(score->dll_handle, pitcher_unload_object);

	SAFE_RELEASE(score, pitcher_free);
}

int set_score_distorted(struct test_node *node, struct test_node *src)
{
	struct score_t *score;

	score = container_of(node, struct score_t, node);

	if (src->pixelformat != score->target_format) {
		PITCHER_ERR("vmaf only supports yuv420p\n");
		return -RET_E_NOT_SUPPORT;
	}

	score->node.width = src->width;
	score->node.height = src->height;
	score->node.pixelformat = src->pixelformat;
	if (src->framerate)
		score->node.framerate = src->framerate;

	memset(&score->format, 0, sizeof(score->format));
	score->format.format = score->node.pixelformat;
	score->format.width = score->node.width;
	score->format.height = score->node.height;
	pitcher_get_pix_fmt_info(&score->format, 0);

	return RET_OK;
}

int parse_score_option(struct test_node *node, struct mxc_vpu_test_option *option, char *argv[])
{
	struct score_t *score;

	if (!node || !option || !option->name)
		return -RET_E_INVAL;
	if (option->arg_num && !argv)
		return -RET_E_INVAL;

	score = container_of(node, struct score_t, node);
	if (!strcasecmp(option->name, "key"))
		score->node.key = strtol(argv[0], NULL, 0);
	else if (!strcasecmp(option->name, "source"))
		score->node.source = strtol(argv[0], NULL, 0);
	else if (!strcasecmp(option->name, "ref"))
		score->ref_key = strtol(argv[0], NULL, 0);
	else if (!strcasecmp(option->name, "libvmaf"))
		score->dll_path = argv[0];
	else if (!strcasecmp(option->name, "output"))
		score->output_path = argv[0];

	return RET_OK;
}

struct test_node *alloc_score_node(void)
{
	struct score_t *score;

	score = pitcher_calloc(1, sizeof(*score));
	if (!score)
		return NULL;

	score->node.key = -1;
	score->node.source = -1;
	score->node.type = TEST_TYPE_ANALYSIS;

	score->reference.chnno = -1;
	score->distorted.chnno = -1;

	score->dll_path = LIBVMAF_LIBRARY_PATH_DEFAULT;
	score->target_format = PIX_FMT_I420;
	score->node.init_node = init_score_node;
	score->node.get_sink_chnno = get_score_distorted_chnno;
	score->node.free_node = free_score_node;
	score->node.set_source = set_score_distorted;
	score->node.pre_start = check_score_reference;

	return &score->node;
}
