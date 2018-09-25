sources = [
    'gstopenhevc.c',
    'gstopenhevcviddec.c',
]

gstopenhevc = library('gstopenhevc',
    sources,
    c_args : gst_openhevc_args,
    include_directories : [configinc],
    dependencies : openhevc_deps + [gst_dep, gstbase_dep, gstvideo_dep,
        gstpbutils_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
