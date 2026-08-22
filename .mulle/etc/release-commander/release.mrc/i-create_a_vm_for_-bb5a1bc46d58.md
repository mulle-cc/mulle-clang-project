
## Create a fresh debian VM (if needed)

* currently 4GB of free file space is needed for a non-debug build
* for debug build multiply by 5, so make it > 20GB
* give it as many CPUs as you can spare
* needs 16GB RAM (sic) at least
* Consider if VM should not have swap space, prefer to crash and reconfigure


Here we are installing into a fresh "trixie" VM  of the same name:

``` bash
scp ~/.ssh/id_rsa_vm.pub trixie:
ssh trixie
mkdir .ssh
mv id_rsa_vm.pub .ssh/authorized_keys
chmod 400 .ssh/authorized_keys
chmod 700 .ssh
```

Add `trixie` to `/etc/hosts` on host.
Add `trixie` to `~/.ssh/config` on host.

> #### Or use an aws instance
>
> Do not skimp on CPU power. `c7g.8xlarge` or better is what you want. Remember
> the build will (until the link) scale almost perfectly, so it can be even
> cheaper to use bigger iron (probably not though because of setup and CPU
> time).
>
> 12GB for compile, 16GB for link
> 24GB space for disk (assuming none taken by OS install)
>
> Checkout [https://nat.prose.sh/p-cb240a1d-d580-4485-85f8-0aed20792d4e](Install AWS CLI in >distrobox), for some steps how to get going with aws. But basically you are
> on your own with respect to this file, but AI will guide you.
> Once you got an EC2 instance up and running and can `ssh` into it. And
> install prerequisites:

> ``` bash
> sudo yum install git clang cmake make ninja-build
> ```
> You can continue now with [One script does all](#One-script-does-all).
>

## Prerequisites

* sudo
* git

On debian, install **git** and get **sudo** happening

``` bash
su
apt-get install git sudo
/sbin/usermod -aG sudo <loginname> # or your login
sudo /sbin/visudo
# Allow members of group sudo to execute any command
%sudo   ALL=(ALL:ALL) NOPASSWD: ALL
# log off now, so sudo group change takes effect
```

Install **cmake** and such things:

``` bash
wget 'https://raw.githubusercontent.com/mulle-cc/mulle-clang-project/mulle/21.1.8/clang/bin/install-prerequisites'
chmod 755 install-prerequisites
./install-prerequisites --no-lldb
```
