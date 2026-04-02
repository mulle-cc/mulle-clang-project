## Edit github-ci projects with mulle-clang URL.

So unfortunately we need to hardcode some URLs in the github scripts. When the compiler changes we need to change them as well. Even more unfortunate is, that we may need multiple compilers:

```
          case "$LSB_RELEASE" in
             plucky|oracular|noble|mantic|lunar|kinetic|trixie|forky|bookworm|2[432]\.*) # broken catthehacker image fix for act
                codename="bookworm"
                version="21.1.8.1" # default
             ;;
```

We already retrieved the new names in a previous setup step, but its not really clear
when to change the version for all "bookworm" compatibles and when we need a second version 
for a successor OS.
 

Projects we need to update are:

* mulle-cc/github-ci
* mulle-objc/github-ci
* MulleFoundation/github-ci


It's a simple replacement. Unfortunately we then also need to change 
the version, which will affect all "github CI files" which then
need to freshened.

The script will just so a simple replacement and push. You may need to do it manually.
After a change to these values, we then have to go into mulle-sde-ci.yml and change

- uses: mulle-cc/github-ci@v5 

in these files 

* mulle-objc-developer/.github/workflows/mulle-sde-ci.yml
* mulle-objc-developer/src/mulle-objc/github-actions/project/all/.github/workflows/mulle-sde-ci.yml
* mulle-foundation-developer/.github/workflows/mulle-sde-ci.yml
* mulle-foundation-developer/src/mulle-foundation/github-actions/project/all/.github/workflows/mulle-sde-ci.yml
* mulle-foundation-developer/src/mulle-foundation/github-actions/project/all/.github/workflows/mulle-sde-artifacts.yml
* foundation-developer/.github/workflows/mulle-sde-ci.yml
* foundation-developer/src/foundation/github-actions/project/all/.github/workflows/mulle-sde-ci.yml
* foundation-developer/src/foundation/github-actions/project/all/.github/workflows/mulle-sde-artifacts.yml
* mulle-web-developer/.github/workflows/mulle-sde-ci.yml

Later we will freshen up the CI files in the project to get the newest version
