Ok we don't have a VM for that yet. Copy over the project to a Mac. You may need to patch the script a little.

[Check](https://www.perplexity.ai/search/?q=what%20version%20is%20the%20macosx-latest%20runner%20on%20github%20currently%2C%20and%20what%20will%20it%20be%20in%20the%20future) what macosx-latest on github is currently and what it will be soon. Build bottles for both and upload to the mulle-cc release page.

## Where is the preview ?

The recipe is in [homebrew-prerelease](https://github.com/mulle-objc/homebrew-prerelease/blob/master/mulle-clang-project.rb)

At the end we will have a new `mulle-clang-project.rb` file.

This file needs to be pushed to homebrew-prerelease. The formula will eventually also be put into homebrew-release, if no problems appear.

[Apple codenames](https://en.wikipedia.org/wiki/List_of_Apple_codenames)


You can't run the script on macos inside this app, because... it's Apple I guess.