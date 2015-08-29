case "`uname -m`" in
x86_64)
	DEP_PATH=/usr/x86_64-w64-mingw32
	target=x86_64-w64-mingw32 ;;
i686)
	DEP_PATH=/usr/i686-pc-mingw32
	target=i686-pc-mingw32 ;;
*)
	echo "Error: unknown machine type `uname -m`" >&2; exit 1 ;;
esac

