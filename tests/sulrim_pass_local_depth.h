// LLM maintained. Restrictions also hold with debug validation disabled or compiled out.
static void check_pass_local_depth_contract(void) {
    const sg_image_desc desc = {.width = 16, .height = 4, .pixel_format = SG_PIXELFORMAT_DEPTH, .sample_count = 1,
        .usage = {.depth_stencil_attachment = true, .pass_local_depth = true}};
    sg_image image = sg_make_image(&desc);
    CHECK(sg_query_image_state(image) == SG_RESOURCESTATE_VALID && sg_query_image_usage(image).pass_local_depth);
    sg_view view = sg_make_view(&(sg_view_desc){.depth_stencil_attachment.image = image});
    CHECK(sg_query_view_state(view) == SG_RESOURCESTATE_VALID);
    expect_error = true;
    const sg_view_desc invalid_views[] = {{.texture.image = image}, {.storage_image.image = image},
        {.color_attachment.image = image}, {.resolve_attachment.image = image}};
    for (unsigned i = 0; i < sizeof invalid_views / sizeof invalid_views[0]; i++) {
        sg_view invalid = sg_make_view(&invalid_views[i]);
        CHECK(sg_query_view_state(invalid) == SG_RESOURCESTATE_FAILED); sg_destroy_view(invalid);
    }
    sg_pass pass = _sg_pass_defaults(&(sg_pass){.attachments.depth_stencil = view});
    CHECK(_sg_validate_begin_pass(&pass));
    pass.action.depth.load_action = SG_LOADACTION_LOAD; CHECK(!_sg_validate_begin_pass(&pass));
    pass.action.depth.load_action = SG_LOADACTION_DONTCARE; CHECK(_sg_validate_begin_pass(&pass));
    pass.action.depth.store_action = SG_STOREACTION_STORE; CHECK(!_sg_validate_begin_pass(&pass));
    sg_image_desc bad[22];
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) bad[i] = desc;
    bad[0].usage.depth_stencil_attachment = false;
    bad[1].usage.color_attachment = true; bad[2].usage.storage_image = true; bad[3].usage.resolve_attachment = true;
    bad[4].usage.write_transient = true; bad[5].usage.write_unsealed = true;
    bad[6].usage.copy_src = true; bad[7].usage.copy_dst = true;
    bad[8].type = SG_IMAGETYPE_ARRAY; bad[9].pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    bad[10].num_mipmaps = 2; bad[11].num_slices = 2; bad[12].sample_count = 4;
    bad[13].data = SG_RANGE(desc); bad[14].mtl_texture = (const void*)1; bad[15].wgpu_texture = (const void*)1;
    bad[16].gl_texture = 1; bad[17].d3d11_texture = (const void*)1;
    bad[18].pixel_format = SG_PIXELFORMAT_DEPTH16; bad[19].data.size = 4;
    bad[20].width = -1; bad[21].height = -1;
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        sg_image invalid = sg_make_image(&bad[i]);
        CHECK(sg_query_image_state(invalid) == SG_RESOURCESTATE_FAILED); sg_destroy_image(invalid);
    }
    expect_error = false;
    sg_image_desc ordinary_desc = desc;
    ordinary_desc.usage.pass_local_depth = false;
    sg_image ordinary = sg_make_image(&ordinary_desc);
    sg_buffer staging = sg_make_buffer(&(sg_buffer_desc){.size = 256, .usage = {.staging_buffer = true, .write_transient = true, .copy_src = true}});
    CHECK(sg_query_image_state(ordinary) == SG_RESOURCESTATE_VALID && sg_query_buffer_state(staging) == SG_RESOURCESTATE_VALID);
    int before = validation_errors;
    expect_error = true;
    sg_copy_buffer_to_image(&(sg_copy_buffer_to_image_desc){.src.buffer = staging, .dst.image = image});
    sg_copy_image_to_image(&(sg_copy_image_to_image_desc){.src.image = image, .dst.image = ordinary});
    sg_copy_image_to_image(&(sg_copy_image_to_image_desc){.src.image = ordinary, .dst.image = image});
    CHECK(validation_errors == before + 3);
    expect_error = false;
    sg_destroy_buffer(staging); sg_destroy_image(ordinary); sg_destroy_view(view); sg_destroy_image(image);
}
