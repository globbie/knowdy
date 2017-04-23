Summary:            Knowdy database
Name:               knowdy
Version:            0.1.1
Release:            1%{?dist}
License:            AGPL
Group:              Applications/Databases 
Source0:            %{name}-%{version}.tar.gz
BuildRoot:          %{_tmppath}-knd-root
BuildArch:          x86_64
BuildRequires:      zeromq-devel
Requires:           zeromq, libxml2
Requires:           initscripts >= 8.36
Requires(pre):      /usr/sbin/useradd
Requires(post):     chkconfig

%description
Knowdy database

%prep
%setup -q -n %{name}-%{version}

%build
make

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT%_bindir
mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/knd/db
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/rc.d/init.d
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/knd

install -m 755 ./knd_coll $RPM_BUILD_ROOT%_bindir
install -m 755 ./knd_storage $RPM_BUILD_ROOT%_bindir
install -m 755 ./knd_deliv $RPM_BUILD_ROOT%_bindir
install -m 755 ./knd_reader $RPM_BUILD_ROOT%_bindir
install -m 755 ./knd_writer $RPM_BUILD_ROOT%_bindir

install -m 755 ./knd-coll.init \
              $RPM_BUILD_ROOT%{_sysconfdir}/rc.d/init.d/knd-coll
install -m 755 ./knd-storage.init \
              $RPM_BUILD_ROOT%{_sysconfdir}/rc.d/init.d/knd-storage
install -m 755 ./knd-deliv.init \
              $RPM_BUILD_ROOT%{_sysconfdir}/rc.d/init.d/knd-deliv
install -m 755 ./knd-reader.init \
              $RPM_BUILD_ROOT%{_sysconfdir}/rc.d/init.d/knd-reader
install -m 755 ./knd-writer.init \
              $RPM_BUILD_ROOT%{_sysconfdir}/rc.d/init.d/knd-reader

install -m 644 ./storage.ini \
              $RPM_BUILD_ROOT%{_sysconfdir}/knd
install -m 644 ./reader.ini \
              $RPM_BUILD_ROOT%{_sysconfdir}/knd
install -m 644 ./writer.ini \
              $RPM_BUILD_ROOT%{_sysconfdir}/knd

%pre
getent group knd >/dev/null || groupadd -g 3330 -r knd
getent passwd knd >/dev/null || \
    useradd -r -u 3330 -g knd -s /sbin/nologin \
    -d /var/knd -c "Knowdy" knd
exit 0

%post
/sbin/chkconfig --add knd-coll
/sbin/chkconfig --add knd-deliv
/sbin/chkconfig --add knd-storage
/sbin/chkconfig --add knd-reader
/sbin/chkconfig --add knd-writer

%preun
if [ $1 = 0 ]; then
    /sbin/service knd-coll stop >/dev/null 2>&1
    /sbin/chkconfig --del knd-coll
    
    /sbin/service knd-storage stop >/dev/null 2>&1
    /sbin/chkconfig --del knd-storage

    /sbin/service knd-deliv stop >/dev/null 2>&1
    /sbin/chkconfig --del knd-deliv

    /sbin/service knd-reader stop >/dev/null 2>&1
    /sbin/chkconfig --del knd-reader

fi

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%_bindir/knd_coll
%_bindir/knd_storage
%_bindir/knd_deliv
%_bindir/knd_reader
%_bindir/knd_writer
%attr(0730,root,knd)%dir %{_localstatedir}/knd/db
%dir %_sysconfdir/knd
%_sysconfdir/rc.d/init.d/knd-coll
%_sysconfdir/rc.d/init.d/knd-storage
%_sysconfdir/rc.d/init.d/knd-deliv
%_sysconfdir/rc.d/init.d/knd-reader
%_sysconfdir/rc.d/init.d/knd-writer
%_sysconfdir/knd/coll.ini
%_sysconfdir/knd/storage.ini
%_sysconfdir/knd/deliv.ini
%_sysconfdir/knd/reader.ini
%_sysconfdir/knd/writer.ini

%changelog
* Sat Apr 23 2017 Dmitri Dmitriev <dmitri@globbie.net>
- initial spec
