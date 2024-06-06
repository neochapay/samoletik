%define __provides_exclude_from ^%{_datadir}/%{name}/.*$
%define __requires_exclude ^lib.*\\.*$

Name:       ru.neochapay.samoletik
Summary:    Simple TG client
Version:    0.0.1
Release:    0
Group:      Qt/Qt
License:    GNU GPLv3
URL:        https://neochapay.ru
Source:     %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  desktop-file-utils
BuildRequires:  librsvg-tools


%description
Simple TG client

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5

make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%qmake5_install

desktop-file-install --delete-original       \
  --dir %{buildroot}%{_datadir}/applications             \
   %{buildroot}%{_datadir}/applications/*.desktop


for size in 86 108 128 172 256
do
   mkdir -p %{buildroot}%{_datadir}/icons/hicolor/${size}x${size}/apps/
   rsvg-convert --width=$size --height=$size --output \
           %{buildroot}%{_datadir}/icons/hicolor/${size}x${size}/apps/%{name}.png \
           %{_sourcedir}/../src/icons/svg/%{name}.svg
done

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%defattr(644,root,root,-)
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
