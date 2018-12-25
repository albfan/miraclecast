This file can be used with the rpmbuild or mock commands to create a binary RPM.  Tested and working on Fedora 28 x86\_64.

Example usage (for Fedora 28 systems):
1. Create a tar.gz file of the source code and add the short name of the commit to the tar file, for the most recent commit at the time of writing this would be:
    * `git clone https://github.com/albfan/miraclecast.git miraclecast-c3c868e`
    * `tar -zcf miraclecast-c3c868e.tar.gz miraclecast-c3c868e`

2. Create a Source RPM
    * `rpmbuild -bs miraclecast.spec --define "_sourcedir $PWD"`

Then build the RPM with one of the following:

* Using mock (assuming you're in the dir where the .src.rpm file is):
    * `mock --arch=x86_64 -r fedora-28-x86_64 --resultdir=results miraclecast-1.0-1.gitc3c868e.fc28.src.rpm`

OR

* Using rpmbuild (assuming you're in the dir where the .src.rpm file is):
    * `rpmbuild -ra miraclecast-1.0-1.gitc3c868e.fc28.src.rpm`

OR

* Using rpmbuild (assuming you're in the dir where the .spec and .tar.gz files are):
    * `rpmbuild -bs miraclecast.spec --define "_sourcedir $PWD"``
