# Waon, just ported to git.

* This repository re-hosts the waon software source code by Kengo Ichiki, ref http://waon.sourceforge.net/ , licensed under GPL v2.
* Technically it is a plain import from https://sourceforge.net/p/waon/code/ using `git cvsimport` plus a `.gitignore` and this README.

## Build instructions TL;DR.

    sudo apt-get install libao-dev libatk1.0-dev libcairo2-dev libfftw3-dev libfontconfig1-dev libfreetype6-dev libgdk-pixbuf2.0-dev libglib2.0-dev libgtk2.0-dev libpango1.0-dev libsamplerate0-dev libsndfile1-dev
    make

Will it work? It compiled on Xubuntu 18.04.

I quickly tested on a piano work:

* `waon` with default parameters produced an imperfect but sensible midi file.
* `gwaon` opened and shows a working GUI.  Playing was sometimes stuttering but GUI worked.

## How build instructions were figured out

The original project is makefile-based.  It finds libs using `pkg-config`.

### What if we automatically scanned the `Makefile`s and get a `apt-get` command line?

I quickly hacked this bash-based nearly-one-liner to figure out which packages would be necessary to compile source code:

<pre>
{ echo "Scanning makefiles for \`pkg-config\` mentions..."
  echo
  PKGS=$( sed 's/#.*$//' Makefile* | sed -n 's/^.*pkg-config  *[^ ]*  *\([^ ]*\) *[^ ] .*$/\1/p' | sort | uniq )
  echo "\`Pkg-config\`s mentioned: \`" $PKGS "\`"
  echo
  echo "Querying this local machine's \`pkg-config\` for library names to search during compilation.  Lines below are its output, specific to your distribution, that you might want to check."
  echo
  LIBS=$( for PKG in $PKGS
          do { pkg-config --libs  $PKG 2>&1 1>&3 | sed 's/^/    /' 1>&2; } 3>&1  | sed -e 's/^-l//' -e 's/ -l/ /g'
          done )
  echo
  echo "Finished querying \`pkg-config\`.  Compile-time libraries available on your Debian:"
  echo
  echo "$LIBS" | sed 's/^/* /'
  echo
  echo "Now querying Debian's \`apt-file\` tool (assuming it is locally installed) to find suggestions of Debian packages necessary to build this software."
  echo
  DEBS=$( { for LIB in $LIBS
            do apt-file find --regexp "/lib${LIB}.so$" & true
            done
          } | sed -n 's/^\([^:]*-dev\):.*$/\1/p' | sort | uniq )
  echo
  echo "Done querying \`apt-file\`.  Here's a suggested command line:"
  echo
  echo "    sudo apt-get install" $DEBS
  echo
  echo "Try installing packages with the command line above, then check \`Makefile\`s and possibly try \`make\`.  There might be some missing packages, hope this little script helped you."
}
</pre>


### Output of script

Scanning makefiles for `pkg-config` mentions...

`Pkg-config`s mentioned: ` ao fftw3 gdk_pixbuf gtk+-2.0 jack samplerate sndfile `

Querying this local machine's `pkg-config` for library names to search during compilation.  Lines below are its output, specific to your distribution, that you might want to check.

    Package gdk_pixbuf was not found in the pkg-config search path.
    Perhaps you should add the directory containing `gdk_pixbuf.pc'
    to the PKG_CONFIG_PATH environment variable
    Package 'gdk_pixbuf', required by 'world', not found
    Package jack was not found in the pkg-config search path.
    Perhaps you should add the directory containing `jack.pc'
    to the PKG_CONFIG_PATH environment variable
    Package 'jack', required by 'world', not found

Finished querying `pkg-config`.  Compile-time libraries available on your Debian:

* ao
* fftw3
* gtk-x11-2.0 gdk-x11-2.0 pangocairo-1.0 atk-1.0 cairo gdk_pixbuf-2.0 gio-2.0 pangoft2-1.0 pango-1.0 gobject-2.0 glib-2.0 fontconfig freetype
* samplerate
* sndfile

Now querying Debian's `apt-file` tool (assuming it is locally installed) to find suggestions of Debian packages necessary to build this software.


Done querying `apt-file`.  Here's a suggested command line:

    sudo apt-get install libao-dev libatk1.0-dev libcairo2-dev libfftw3-dev libfontconfig1-dev libfreetype6-dev libgdk-pixbuf2.0-dev libglib2.0-dev libgtk2.0-dev libpango1.0-dev libsamplerate0-dev libsndfile1-dev

Try installing packages with the command line above, then check `Makefile`s and possibly try `make`.  There might be some missing packages, hope this little script helped you.

## Now what?

You can play with the software.
That's all folks.
