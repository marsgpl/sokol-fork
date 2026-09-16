// Shared contract checks; run with debug validation enabled, disabled and compiled out.
static void check_pass_local_depth_contract(void) {
    const sg_image_desc desc = {
        .width = 16, .height = 4, .pixel_format = SG_PIXELFORMAT_DEPTH, .sample_count = 1,
        .usage = {.depth_stencil_attachment = true, .pass_local_depth = true},
    };
    sg_image image = sg_make_image(&desc);
    assert(sg_query_image_state(image) == SG_RESOURCESTATE_VALID);
    assert(sg_query_image_usage(image).pass_local_depth);
    sg_view view = sg_make_view(&(sg_view_desc){.depth_stencil_attachment.image = image});
    assert(sg_query_view_state(view) == SG_RESOURCESTATE_VALID);
    const sg_view_desc invalid_views[] = {
        {.texture.image = image}, {.storage_image.image = image},
        {.color_attachment.image = image}, {.resolve_attachment.image = image},
    };
    for (unsigned i = 0; i < sizeof invalid_views / sizeof invalid_views[0]; i++) {
        sg_view invalid = sg_make_view(&invalid_views[i]);
        assert(sg_query_view_state(invalid) == SG_RESOURCESTATE_FAILED);
        sg_destroy_view(invalid);
    }
    sg_pass pass = _sg_pass_defaults(&(sg_pass){.attachments.depth_stencil = view});
    assert(_sg_validate_begin_pass(&pass));
    pass.action.depth.load_action = SG_LOADACTION_LOAD;
    assert(!_sg_validate_begin_pass(&pass));
    pass.action.depth.load_action = SG_LOADACTION_DONTCARE;
    assert(_sg_validate_begin_pass(&pass));
    pass.action.depth.store_action = SG_STOREACTION_STORE;
    assert(!_sg_validate_begin_pass(&pass));
    sg_image_desc bad[15];
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) bad[i] = desc;
    bad[0].usage.depth_stencil_attachment = false;
    bad[1].usage.color_attachment = true;
    bad[2].usage.storage_image = true;
    bad[3].usage.resolve_attachment = true;
    bad[4].usage.dynamic_update = true;
    bad[5].usage.stream_update = true;
    bad[6].usage.gpu_write_only = true;
    bad[7].usage.write_unsealed = true;
    bad[8].type = SG_IMAGETYPE_ARRAY;
    bad[9].pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    bad[10].num_mipmaps = 2;
    bad[11].num_slices = 2;
    bad[12].sample_count = 4;
    bad[13].data.mip_levels[1] = SG_RANGE(desc);
    bad[14].mtl_textures[0] = (const void*)1;
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        sg_image invalid = sg_make_image(&bad[i]);
        assert(sg_query_image_state(invalid) == SG_RESOURCESTATE_FAILED);
        sg_destroy_image(invalid);
    }
    sg_destroy_view(view);
    sg_destroy_image(image);
}
