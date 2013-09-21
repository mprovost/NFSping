%define git_rel e2d35d4

Name:           nfsping
Version:        0.1
Release:        1.git%{git_rel}%{?dist}
Summary:        NFSping is a command line utility for measuring the response time of an NFS server. 
Group:          Applications/Internet
License:        BSD
URL:            http://github.com/mprovost/NFSping
# git export from http://github.com/mprovost/NFSping/tarball/master
Source0:        http://download.github.com/mprovost-NFSping-%{git_rel}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

%description
NFSping is a command line utility for measuring the response time of an NFS server. It's basically a copy of the fping interface but doesn't share any code with that project.

On modern NFS servers, the network stack and filesystem are often being run on separate cores or even hardware components. This means in practise that a fast ICMP ping response isn't indicative of how quickly the NFS filesystem is responding. This tool more directly tests the NFS component of the operating system.


%prep
%setup -q -n mprovost-NFSping-%{git_rel}

%build
cd src
make %{?_smp_mflags}


%install
rm -rf %{buildroot}
%{__mkdir} -p %{buildroot}%{_bindir}
%{__mkdir} -p %{buildroot}%{_datadir}/smokeping/lib/Smokeping/probes
%{__install} -m 755 src/nfsping %{buildroot}%{_bindir}/nfsping
%{__install} -p -m 644 Smokeping/NFSping.pm %{buildroot}%{_datadir}/smokeping/lib/Smokeping/probes/NFSping.pm

%clean
rm -rf %{buildroot}


%files
%defattr(-,root,root,-)
%{_bindir}/nfsping
%{_datadir}/smokeping/lib/Smokeping/probes/NFSping.pm
%doc README


%changelog
* Mon Nov 15 2010 James Braid <jamesb@loreland.org> 0.1-1.gite2d35d4
-  Initial package
