LIB_PATH=$(pwd)/libs

while [[ "$#" -gt 0 ]]; do
  echo $1
  case "$1" in
    --sdl2_win)
      rm $LIB_PATH/windows/include/*/begin_code.h $LIB_PATH/windows/include/*/close_code.h $LIB_PATH/windows/include/*/SDL*.h $LIB_PATH/windows/lib/*/SDL2.* $LIB_PATH/windows/lib/*/SDL2main.*
      shift
      ;;
    --sdl2_mac)
      rm -r $LIB_PATH/mac/Frameworks/SDL2.framework
      shift
      ;;
    --sdl_ttf_win)
      rm $LIB_PATH/windows/include/SDL_ttf.h $LIB_PATH/windows/lib/*/SDL2_ttf.*
      shift
      ;;
    --detours_win)
      rm $LIB_PATH/windows/include/detver.h $LIB_PATH/windows/include/detours.h $LIB_PATH/windows/lib/*/detours.*
      shift
      ;;
    --discord-rpc_win)
      rm $LIB_PATH/windows/include/discord_*.h $LIB_PATH/windows/lib/*/discord-rpc.*
      shift
      ;;
    --discord-rpc_mac)
      rm $LIB_PATH/mac/include/discord_*.h $LIB_PATH/mac/lib/libdiscord-rpc.a
      shift
      ;;
    --opus_win)
      rm $LIB_PATH/windows/include/opus*.h $LIB_PATH/windows/lib/*/opus.*
      shift
      ;;
    --opus_mac)
      rm $LIB_PATH/mac/include/opus*.h $LIB_PATH/mac/lib/libopus.a
      shift
      ;;
    --openssl_win)
      rm -r $LIB_PATH/windows/include/*/openssl
      rm $LIB_PATH/windows/lib/*/libcrypto* $LIB_PATH/windows/lib/*/libssl*
      shift
      ;;
    --openssl_mac)
      rm -r $LIB_PATH/mac/include/openssl
	    rm $LIB_PATH/mac/lib/libssl.a $LIB_PATH/mac/lib/libcrypto.a
      shift
      ;;
    --ffmpeg_win)
      rm -r $LIB_PATH/windows/include/*/libavcodec $LIB_PATH/windows/include/*/libavutil
      rm $LIB_PATH/windows/lib/*/avcodec* $LIB_PATH/windows/lib/*/avutil*
      shift
      ;;
    --ffmpeg_mac)
      rm -r $LIB_PATH/mac/include/libavcodec $LIB_PATH/mac/include/libavutil
      rm $LIB_PATH/mac/lib/libavcodec* $LIB_PATH/mac/lib/libavutil*
      shift
      ;;
    --)
      shift;
      break
      ;;
    *)
      echo "Unexpected option: $1"
      exit
      ;;
  esac
done