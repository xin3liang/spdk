jjjjjjjjjjjh
_create_lvol_disk_destroy_cb(void *cb_arg, int bdeverrno)
{
	struct spdk_lvol *lvol = cb_arg;

	if (bdeverrno < 0) {
		SPDK_ERRLOG("Could not unregister bdev for lvol %s\n",
			    lvol->unique_id);
		return;
	}

	spdk_lvol_destroy(lvol, _spdk_lvol_destroy_cb, NULL);
}

static void
_create_lvol_disk_unload_cb(void *cb_arg, int bdeverrno)
{
	struct spdk_lvol *lvol = cb_arg;

	if (bdeverrno < 0) {
		SPDK_ERRLOG("Could not unregister bdev for lvol %s\n",
			    lvol->unique_id);
		return;
	}

	TAILQ_REMOVE(&lvol->lvol_store->lvols, lvol, link);
	free(lvol);
}

static int
_create_lvol_disk(struct spdk_lvol *lvol, bool destroy)
{
	struct spdk_bdev *bdev;
	struct lvol_store_bdev *lvs_bdev;
	uint64_t total_size;
	unsigned char *alias;
	int rc;

	lvs_bdev = vbdev_get_lvs_bdev_by_lvs(lvol->lvol_store);
	if (lvs_bdev == NULL) {
		SPDK_ERRLOG("No spdk lvs-bdev pair found for lvol %s\n", lvol->unique_id);
		return -ENODEV;
	}

	bdev = calloc(1, sizeof(struct spdk_bdev));
	if (!bdev) {
		SPDK_ERRLOG("Cannot alloc memory for lvol bdev\n");
		return -ENOMEM;
	}

	bdev->name = lvol->name;
	bdev->product_name = "Logical Volume";
	bdev->blocklen = spdk_bs_get_io_unit_size(lvol->lvol_store->blobstore);
	total_size = spdk_blob_get_num_clusters(lvol->blob) *
		     spdk_bs_get_cluster_size(lvol->lvol_store->blobstore);
	assert((total_size % bdev->blocklen) == 0);
	bdev->blockcnt = total_size / bdev->blocklen;
	bdev->uuid = lvol->uuid;
	bdev->required_alignment = lvs_bdev->bdev->required_alignment;
	bdev->split_on_optimal_io_boundary = true;
	bdev->optimal_io_boundary = spdk_bs_get_cluster_size(lvol->lvol_store->blobstore) / bdev->blocklen;

	bdev->ctxt = lvol;
	bdev->fn_table = &vbdev_lvol_fn_table;
	bdev->module = &g_lvol_if;

	rc = spdk_bdev_register(bdev);
	if (rc) {
		free(bdev);
		return rc;
	}
	lvol->bdev = bdev;

	alias = spdk_sprintf_alloc("%s/%s", lvs_bdev->lvs->name, lvol->name);
	if (alias == NULL) {
		SPDK_ERRLOG("Cannot alloc memory for alias\n");
		spdk_bdev_unregister(lvol->bdev, (destroy ? _create_lvol_disk_destroy_cb :
						  _create_lvol_disk_unload_cb), lvol);
		return -ENOMEM;
	}

	rc = spdk_bdev_alias_add(bdev, alias);
	if (rc != 0) {
		SPDK_ERRLOG("Cannot add alias to lvol bdev\n");
		spdk_bdev_unregister(lvol->bdev, (destroy ? _create_lvol_disk_destroy_cb :
						  _create_lvol_disk_unload_cb), lvol);
	}
	free(alias);

	return rc;
}

static void
_vbdev_lvol_create_cb(void *cb_arg, struct spdk_lvol *lvol, int lvolerrno)
{
	struct spdk_lvol_with_handle_req *req = cb_arg;

	if (lvolerrno < 0) {
		goto end;
	}

	lvolerrno = _create_lvol_disk(lvol, true);

end:
	req->cb_fn(req->cb_arg, lvol, lvolerrno);
	free(req);
}

int
vbdev_lvol_create(struct spdk_lvol_store *lvs, const char *name, uint64_t sz,
		  bool thin_provision, enum lvol_clear_method clear_method, spdk_lvol_op_with_handle_complete cb_fn,
		  void *cb_arg)
{
	struct spdk_lvol_with_handle_req *req;
	int rc;

	req = calloc(1, sizeof(*req));
	if (req == NULL) {
		return -ENOMEM;
	}
	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;

	rc = spdk_lvol_create(lvs, name, sz, thin_provision, clear_method,
			      _vbdev_lvol_create_cb, req);
	if (rc != 0) {
		free(req);
	}

	return rc;
}

void
vbdev_lvol_create_snapshot(struct spdk_lvol *lvol, const char *snapshot_name,
			   spdk_lvol_op_with_handle_complete cb_fn, void *cb_arg)
{
	struct spdk_lvol_with_handle_req *req;

	req = calloc(1, sizeof(*req));
	if (req == NULL) {
		cb_fn(cb_arg, NULL, -ENOMEM);
		return;
	}

	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;

	spdk_lvol_create_snapshot(lvol, snapshot_name, _vbdev_lvol_create_cb, req);
}

void
vbdev_lvol_create_clone(struct spdk_lvol *lvol, const char *clone_name,
			spdk_lvol_op_with_handle_complete cb_fn, void *cb_arg)
{
	struct spdk_lvol_with_handle_req *req;

	req = calloc(1, sizeof(*req));
	if (req == NULL) {
		cb_fn(cb_arg, NULL, -ENOMEM);
		return;
	}

	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;

	spdk_lvol_create_clone(lvol, clone_name, _vbdev_lvol_create_cb, req);
}

static void
_vbdev_lvol_rename_cb(void *cb_arg, int lvolerrno)
{
	struct spdk_lvol_req *req = cb_arg;

	if (lvolerrno != 0) {
		SPDK_ERRLOG("Renaming lvol failed\n");
	}

	req->cb_fn(req->cb_arg, lvolerrno);
	free(req);
}

void
vbdev_lvol_rename(struct spdk_lvol *lvol, const char *new_lvol_name,
		  spdk_lvol_op_complete cb_fn, void *cb_arg)
{
	struct spdk_lvol_req *req;
	int rc;

	rc = _vbdev_lvol_change_bdev_alias(lvol, new_lvol_name);
	if (rc != 0) {
		SPDK_ERRLOG("renaming lvol to '%s' does not succeed\n", new_lvol_name);
		cb_fn(cb_arg, rc);
		return;
	}

	req = calloc(1, sizeof(*req));
	if (req == NULL) {
		cb_fn(cb_arg, -ENOMEM);
		return;
	}
	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;

	spdk_lvol_rename(lvol, new_lvol_name, _vbdev_lvol_rename_cb, req);
}

static void
_vbdev_lvol_resize_cb(void *cb_arg, int lvolerrno)
{
	struct spdk_lvol_req *req = cb_arg;
	struct spdk_lvol *lvol = req->lvol;
	uint64_t total_size;

	/* change bdev size */
	if (lvolerrno != 0) {
		SPDK_ERRLOG("CB function for bdev lvol %s receive error no: %d.\n", lvol->name, lvolerrno);
		goto finish;
	}

	total_size = spdk_blob_get_num_clusters(lvol->blob) *
		     spdk_bs_get_cluster_size(lvol->lvol_store->blobstore);
	assert((total_size % lvol->bdev->blocklen) == 0);

	lvolerrno = spdk_bdev_notify_blockcnt_change(lvol->bdev, total_size / lvol->bdev->blocklen);
	if (lvolerrno != 0) {
		SPDK_ERRLOG("Could not change num blocks for bdev lvol %s with error no: %d.\n",
			    lvol->name, lvolerrno);
	}

finish:
	req->cb_fn(req->cb_arg, lvolerrno);
	free(req);
}

void
vbdev_lvol_resize(struct spdk_lvol *lvol, uint64_t sz, spdk_lvol_op_complete cb_fn, void *cb_arg)
{
	struct spdk_lvol_req *req;

	if (lvol == NULL) {
		SPDK_ERRLOG("lvol does not exist\n");
		cb_fn(cb_arg, -EINVAL);
		return;
	}

	assert(lvol->bdev != NULL);

	req = calloc(1, sizeof(*req));
	if (req == NULL) {
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;
	req->sz = sz;
	req->lvol = lvol;

	spdk_lvol_resize(req->lvol, req->sz, _vbdev_lvol_resize_cb, req);
}

static void
_vbdev_lvol_set_read_only_cb(void *cb_arg, int lvolerrno)
{
	struct spdk_lvol_req *req = cb_arg;
	struct spdk_lvol *lvol = req->lvol;

	if (lvolerrno != 0) {
		SPDK_ERRLOG("Could not set bdev lvol %s as read only due to error: %d.\n", lvol->name, lvolerrno);
	}

	req->cb_fn(req->cb_arg, lvolerrno);
	free(req);
}

void
vbdev_lvol_set_read_only(struct spdk_lvol *lvol, spdk_lvol_op_complete cb_fn, void *cb_arg)
{
	struct spdk_lvol_req *req;

	if (lvol == NULL) {
		SPDK_ERRLOG("lvol does not exist\n");
		cb_fn(cb_arg, -EINVAL);
		return;
	}

	assert(lvol->bdev != NULL);

	req = calloc(1, sizeof(*req));
	if (req == NULL) {
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;
	req->lvol = lvol;

	spdk_lvol_set_read_only(lvol, _vbdev_lvol_set_read_only_cb, req);
}

static int
vbdev_lvs_init(void)
{
	return 0;
}

static int
vbdev_lvs_get_ctx_size(void)
{
	return sizeof(struct lvol_task);
}

static void
_vbdev_lvs_examine_failed(void *cb_arg, int lvserrno)
{
	struct spdk_lvs_with_handle_req *req = (struct spdk_lvs_with_handle_req *)cb_arg;
	if (lvserrno != 0) {
		SPDK_ERRLOG("Failed to unload lvolstore upon import error\n");
	}
	req->cb_fn(req->cb_arg, NULL, req->lvserrno);
	free(req);
}

static void
_vbdev_lvs_examine_finish(void *cb_arg, struct spdk_lvol *lvol, int lvolerrno)
{
	struct spdk_lvs_with_handle_req *req = (struct spdk_lvs_with_handle_req *)cb_arg;
	struct spdk_lvol_store *lvs = req->lvol_store;

	if (lvolerrno != 0) {
		SPDK_ERRLOG("Error opening lvol %s\n", lvol->unique_id);
		TAILQ_REMOVE(&lvs->lvols, lvol, link);
		lvs->lvol_count--;
		free(lvol);
		goto end;
	}

	if (_create_lvol_disk(lvol, false)) {
		SPDK_ERRLOG("Cannot create bdev for lvol %s\n", lvol->unique_id);
		lvs->lvol_count--;
		SPDK_INFOLOG(SPDK_LOG_VBDEV_LVOL, "Opening lvol %s failed\n", lvol->unique_id);
		goto end;
	}

	lvs->lvols_opened++;
	SPDK_INFOLOG(SPDK_LOG_VBDEV_LVOL, "Opening lvol %s succeeded\n", lvol->unique_id);

end:

	if (lvs->lvols_opened >= lvs->lvol_count) {
		SPDK_INFOLOG(SPDK_LOG_VBDEV_LVOL, "Opening lvols finished\n");
		req->cb_fn(req->cb_arg, lvs, 0);
		free(req);
	}
}

static void
_vbdev_lvs_examine_cb(void *arg, struct spdk_lvol_store *lvol_store, int lvserrno)
{
	struct lvol_store_bdev *lvs_bdev;
	struct spdk_lvs_with_handle_req *req = (struct spdk_lvs_with_handle_req *)arg;
	struct spdk_lvol *lvol, *tmp;

	if (lvserrno == -EEXIST) {
		SPDK_INFOLOG(SPDK_LOG_VBDEV_LVOL,
			     "Name for lvolstore on device %s conflicts with name for already loaded lvs\n",
			     req->base_bdev->name);
		/* On error blobstore destroys bs_dev itself */
		goto end;
	} else if (lvserrno != 0) {
		SPDK_INFOLOG(SPDK_LOG_VBDEV_LVOL, "Lvol store not found on %s\n", req->base_bdev->name);
		/* On error blobstore destroys bs_dev itself */
		goto end;
	}

	lvserrno = spdk_bs_bdev_claim(lvol_store->bs_dev, &g_lvol_if);
	if (lvserrno != 0) {
		SPDK_INFOLOG(SPDK_LOG_VBDEV_LVOL, "Lvol store base bdev already claimed by another bdev\n");
		req->lvserrno = lvserrno;
		spdk_lvs_unload(lvol_store, _vbdev_lvs_examine_failed, req);
		return;
	}

	lvs_bdev = calloc(1, sizeof(*lvs_bdev));
	if (!lvs_bdev) {
		SPDK_ERRLOG("Cannot alloc memory for lvs_bdev\n");
		req->lvserrno = ENOMEM;
		spdk_lvs_unload(lvol_store, _vbdev_lvs_examine_failed, req);
		return;
	}

	lvs_bdev->lvs = lvol_store;
	lvs_bdev->bdev = req->base_bdev;

	TAILQ_INSERT_TAIL(&g_spdk_lvol_pairs, lvs_bdev, lvol_stores);

	SPDK_INFOLOG(SPDK_LOG_VBDEV_LVOL, "Lvol store found on %s - begin parsing\n",
		     req->base_bdev->name);

	lvol_store->lvols_opened = 0;

	if (TAILQ_EMPTY(&lvol_store->lvols)) {
		SPDK_INFOLOG(SPDK_LOG_VBDEV_LVOL, "Lvol store examination done\n");
	} else {
		/* Open all lvols */
		req->lvol_store = lvol_store;
		TAILQ_FOREACH_SAFE(lvol, &lvol_store->lvols, link, tmp) {
			spdk_lvol_open(lvol, _vbdev_lvs_examine_finish, req);
		}
		return;
	}

end:
	req->cb_fn(req->cb_arg, lvol_store, lvserrno);
	free(req);
}

int
vbdev_lvs_examine(struct spdk_bdev *bdev, spdk_lvs_op_with_handle_complete cb_fn, void *cb_arg)
{
	struct spdk_bs_dev *bs_dev;
	struct spdk_lvs_with_handle_req *req;

	req = calloc(1, sizeof(*req));
	if (req == NULL) {
		SPDK_ERRLOG("Cannot alloc memory for vbdev lvol store request pointer\n");
		return -1;
	}

	bs_dev = spdk_bdev_create_bs_dev(bdev, vbdev_lvs_hotremove_cb, bdev);
	if (!bs_dev) {
		SPDK_INFOLOG(SPDK_LOG_VBDEV_LVOL, "Cannot create bs dev on %s\n", bdev->name);
		free(req);
		return -1;
	}

	req->base_bdev = bdev;
	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;

	spdk_lvs_load(bs_dev, _vbdev_lvs_examine_cb, req);
	return 0;
}

struct spdk_lvol *
vbdev_lvol_get_from_bdev(struct spdk_bdev *bdev)
{
	if (!bdev || bdev->module != &g_lvol_if) {
		return NULL;
	}

	if (bdev->ctxt == NULL) {
		SPDK_ERRLOG("No lvol ctx assigned to bdev %s\n", bdev->name);
		return NULL;
	}

	return (struct spdk_lvol *)bdev->ctxt;
}

SPDK_LOG_REGISTER_COMPONENT("vbdev_lvol", SPDK_LOG_VBDEV_LVOL);

int
vbdev_lvol_create_with_uuid(struct spdk_lvol_store *lvs, const char *name, uint64_t sz,
			    bool thin_provision, enum lvol_clear_method clear_method,
			    const char *uuid, spdk_lvol_op_with_handle_complete cb_fn,
			    void *cb_arg)
{
	struct spdk_lvol_with_handle_req *req;
	int rc;

	req = calloc(1, sizeof(*req));
	if (req == NULL) {
		return -ENOMEM;
	}
	req->cb_fn = cb_fn;
	req->cb_arg = cb_arg;

	rc = spdk_lvol_create_with_uuid(lvs, name, sz, thin_provision, clear_method,
					uuid, _vbdev_lvol_create_cb, req);
	if (rc != 0) {
		free(req);
	}

	return rc;
}
