
########### install files ###############

install(
    FILES miracle-gst gstplayer uibc-viewer
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_WRITE GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    DESTINATION bin
    )



#original Makefile.am contents follow:

#bin_SCRIPTS = miracle-gst.sh
#EXTRA_DIST = wpa.conf
